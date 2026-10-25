#include "ShaderAssistant.h"

#include <algorithm>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QSettings>
#include <QUrl>

#include "ShaderStudio.h"

namespace ai = vfx::editor::shader::ai;
namespace sh = vfx::editor::shader;

namespace {

QString text(const std::string& s) { return QString::fromUtf8(s.c_str()); }
std::string utf8(const QString& s) { return s.toUtf8().toStdString(); }

constexpr int kKeepTurns = 6;

}  // namespace

ShaderAssistant::ShaderAssistant(ShaderStudio* studio, QObject* parent) : QObject(parent), studio_(studio) {
    net_ = new QNetworkAccessManager(this);
    load();
}

ShaderAssistant::~ShaderAssistant() = default;

void ShaderAssistant::load() {
    QSettings settings;
    config_.provider = settings.value(QStringLiteral("ai/provider"), 0).toInt() == 1 ? ai::Provider::OpenAiCompatible : ai::Provider::Claude;
    config_.model = utf8(settings.value(QStringLiteral("ai/model")).toString());
    config_.baseUrl = utf8(settings.value(QStringLiteral("ai/baseUrl")).toString());
    key_ = settings.value(QStringLiteral("ai/key") + QString::number(provider())).toString();
    if (key_.isEmpty()) {
        // A key in the environment works too, for people who keep theirs there.
        const auto env = QProcessEnvironment::systemEnvironment();
        key_ = env.value(config_.provider == ai::Provider::Claude ? QStringLiteral("ANTHROPIC_API_KEY") : QStringLiteral("OPENAI_API_KEY"));
    }
}

QString ShaderAssistant::model() const {
    if (!config_.model.empty()) return text(config_.model);
    return config_.provider == ai::Provider::Claude ? QStringLiteral("claude-sonnet-5-5") : QStringLiteral("gpt-4o");
}

bool ShaderAssistant::configured() const {
    if (!key_.isEmpty()) return true;
    // A server on this computer or network usually needs no key.
    return config_.provider == ai::Provider::OpenAiCompatible && !config_.baseUrl.empty();
}

QString ShaderAssistant::providerNote() const {
    return config_.provider == ai::Provider::Claude
               ? QStringLiteral("Claude (Anthropic). Your key is sent only to api.anthropic.com.")
               : QStringLiteral("OpenAI or any compatible server. Your key is sent only to the address you give.");
}

void ShaderAssistant::configure(int provider, const QString& model, const QString& baseUrl, const QString& key) {
    QSettings settings;
    const bool claude = provider != 1;
    settings.setValue(QStringLiteral("ai/provider"), claude ? 0 : 1);
    settings.setValue(QStringLiteral("ai/model"), model.trimmed());
    settings.setValue(QStringLiteral("ai/baseUrl"), baseUrl.trimmed());
    if (!key.trimmed().isEmpty()) settings.setValue(QStringLiteral("ai/key") + QString::number(claude ? 0 : 1), key.trimmed());
    load();
    emit configChanged();
    say(configured() ? QStringLiteral("Connected to %1 (%2).").arg(claude ? QStringLiteral("Claude") : QStringLiteral("the OpenAI-compatible server"), this->model())
                     : QStringLiteral("Saved, but there is still no API key, so nothing can be sent."),
        !configured());
}

void ShaderAssistant::forgetKey() {
    QSettings settings;
    settings.remove(QStringLiteral("ai/key") + QString::number(provider()));
    load();
    emit configChanged();
    say(QStringLiteral("The saved key was removed from this computer."));
}

void ShaderAssistant::say(const QString& message, bool error) {
    message_ = message;
    messageError_ = error;
    emit messageChanged();
}

QVariantList ShaderAssistant::historyList() const {
    QVariantList out;
    for (const ai::Turn& t : history_) {
        QVariantMap m;
        m[QStringLiteral("user")] = text(t.user);
        m[QStringLiteral("assistant")] = text(t.assistant);
        out << m;
    }
    return out;
}

void ShaderAssistant::newConversation() {
    history_.clear();
    dropProposal();
    answer_.clear();
    emit historyChanged();
    emit proposalChanged();
    say(QString());
}

void ShaderAssistant::dropProposal() {
    proposal_.reset();
    summary_.clear();
    explanation_.clear();
    changes_.clear();
    refused_.clear();
    studio_->clearProposal();
}

void ShaderAssistant::cancel() {
    if (!reply_) return;
    QNetworkReply* r = reply_;
    reply_ = nullptr;
    r->disconnect(this);
    r->abort();
    r->deleteLater();
    emit busyChanged();
    say(QStringLiteral("Cancelled. Nothing was changed."));
}

void ShaderAssistant::send(const QString& promptText, const QString& modeName) {
    const QString prompt = promptText.trimmed();
    if (reply_) return;
    if (prompt.isEmpty()) {
        say(QStringLiteral("Describe what you want first."), true);
        return;
    }
    if (!configured()) {
        say(QStringLiteral("No AI model is connected, so nothing was sent. Open Connect below and add an API key."), true);
        return;
    }
    ai::Mode mode = ai::Mode::Generate;
    if (modeName == QLatin1String("refine")) mode = ai::Mode::Refine;
    else if (modeName == QLatin1String("animate")) mode = ai::Mode::Animate;
    else if (modeName == QLatin1String("explain")) mode = ai::Mode::Explain;
    if (mode != ai::Mode::Generate && !studio_->isLayers()) {
        say(QStringLiteral("The open shader is not a layer shader, so there are no layers to change. Use Generate Shader to build a new one."), true);
        return;
    }
    dropProposal();
    answer_.clear();
    emit proposalChanged();

    pendingMode_ = mode;
    pendingContext_.settings = studio_->settings();
    pendingContext_.selectedLayer = studio_->isLayers() ? studio_->selectedLayer() : -1;
    pendingContext_.mode = mode;
    pendingPrompt_ = prompt;
    lastPrompt_ = prompt;
    lastMode_ = modeName;

    // Only the latest turns travel with a follow-up; the current state is always sent fresh.
    std::vector<ai::Turn> recent = history_;
    if (recent.size() > static_cast<std::size_t>(kKeepTurns)) recent.erase(recent.begin(), recent.end() - kKeepTurns);
    const ai::HttpRequest http = ai::buildRequest(config_, pendingContext_, utf8(prompt), recent);

    QNetworkRequest request{QUrl(text(http.url))};
    for (const auto& [name, value] : http.headers) request.setRawHeader(QByteArray::fromStdString(name), QByteArray::fromStdString(value));
    if (!key_.isEmpty()) {
        if (config_.provider == ai::Provider::Claude) request.setRawHeader("x-api-key", key_.toUtf8());
        else request.setRawHeader("Authorization", "Bearer " + key_.toUtf8());
    }
    request.setTransferTimeout(150000);
    reply_ = net_->post(request, QByteArray::fromStdString(http.body));
    connect(reply_, &QNetworkReply::finished, this, [this] { finished(); });
    emit busyChanged();
    say(mode == ai::Mode::Explain ? QStringLiteral("Reading the shader...") : QStringLiteral("Asking the model... this can take up to a minute."));
}

void ShaderAssistant::regenerate() {
    if (lastPrompt_.isEmpty() || reply_) return;
    send(lastPrompt_, lastMode_);
}

void ShaderAssistant::finished() {
    QNetworkReply* r = reply_;
    if (!r) return;
    reply_ = nullptr;
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = r->readAll();
    const QNetworkReply::NetworkError error = r->error();
    const QString errorText = r->errorString();
    r->deleteLater();
    emit busyChanged();
    if (status == 0 && error != QNetworkReply::NoError) {
        say(QStringLiteral("Could not reach the AI service: %1").arg(errorText), true);
        return;
    }
    showResult(ai::parseReply(config_, body.toStdString(), status));
}

void ShaderAssistant::showResult(const ai::Reply& reply) {
    if (!reply.error.empty()) {
        say(QStringLiteral("The AI service said: %1").arg(text(reply.error)), true);
        return;
    }
    if (pendingMode_ == ai::Mode::Explain) {
        answer_ = text(reply.text);
        if (answer_.isEmpty()) {
            say(QStringLiteral("The model sent no explanation."), true);
            return;
        }
        history_.push_back({utf8(pendingPrompt_), reply.text.substr(0, 400)});
        emit historyChanged();
        emit proposalChanged();
        say(QStringLiteral("Explained. Nothing was changed."));
        return;
    }
    if (!reply.hasPlan) {
        say(reply.text.empty() ? QStringLiteral("The model did not send any shader changes. Try rewording the request.")
                               : QStringLiteral("The model answered in words only: %1").arg(text(reply.text).left(300)),
            true);
        return;
    }
    ai::Applied applied = ai::applyPlan(pendingContext_, reply.plan);
    summary_ = text(reply.plan.summary);
    explanation_ = text(reply.plan.explanation);
    changes_.clear();
    refused_.clear();
    for (const std::string& c : applied.changes) changes_ << text(c);
    for (const std::string& c : applied.refused) refused_ << text(c);
    if (applied.applied == 0) {
        answer_.clear();
        emit proposalChanged();
        say(QStringLiteral("None of the model's commands could be applied, so nothing changed. You can regenerate, or reword the request."), true);
        return;
    }
    proposalSlot_ = applied.touchedLayers.empty() ? -1 : applied.touchedLayers.front();
    // Show the top touched layer when something was added, so the panel opens on it.
    if (!applied.touchedLayers.empty()) proposalSlot_ = *std::max_element(applied.touchedLayers.begin(), applied.touchedLayers.end());
    studio_->showProposal(applied.settings);
    proposal_ = std::make_unique<ai::Applied>(std::move(applied));
    emit proposalChanged();
    say(QStringLiteral("Proposal ready: %1 change%2%3. It is shown in the preview, not applied yet.")
            .arg(changes_.size())
            .arg(changes_.size() == 1 ? QString() : QStringLiteral("s"))
            .arg(refused_.isEmpty() ? QString() : QStringLiteral(", %1 command%2 refused").arg(refused_.size()).arg(refused_.size() == 1 ? QString() : QStringLiteral("s"))));
}

void ShaderAssistant::apply() {
    if (!proposal_) return;
    const QString label = QStringLiteral("AI: %1").arg(summary_.isEmpty() ? QStringLiteral("changes applied") : summary_);
    history_.push_back({utf8(pendingPrompt_), utf8(summary_)});
    studio_->applyExternal(proposal_->settings, label, proposalSlot_);
    proposal_.reset();
    changes_.clear();
    refused_.clear();
    summary_.clear();
    emit historyChanged();
    emit proposalChanged();
    say(QStringLiteral("Applied. Undo in the toolbar takes it back in one step."));
}

void ShaderAssistant::reject() {
    if (!proposal_) return;
    dropProposal();
    emit proposalChanged();
    say(QStringLiteral("Dropped. Your shader was not changed."));
}
