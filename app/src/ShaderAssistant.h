// The AI Shader Artist panel's model: connects the Shader tab to a language model.
//
// It asks the model, with the shader's real state and the real parts list, for edit commands;
// the editor layer (vfx/editor/ShaderAi.h) checks and applies them to a copy; the copy is shown
// in the preview as a proposal until the artist applies it (one undo step) or drops it.
//
// Keys: the API key is kept on this computer only, in the app's settings (not encrypted), and
// is sent only to the provider the artist chose. It is never written into a shader file, an
// export or the source. Without a key (or a local server address) nothing is sent and the panel
// says so.
#pragma once

#include <memory>

#include <QObject>
#include <QStringList>
#include <QVariantList>

#include "vfx/editor/ShaderAi.h"

class QNetworkAccessManager;
class QNetworkReply;
class ShaderStudio;

class ShaderAssistant : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool configured READ configured NOTIFY configChanged)
    Q_PROPERTY(int provider READ provider NOTIFY configChanged)
    Q_PROPERTY(QString model READ model NOTIFY configChanged)
    Q_PROPERTY(QString baseUrl READ baseUrl NOTIFY configChanged)
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY configChanged)
    Q_PROPERTY(QString providerNote READ providerNote NOTIFY configChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
    Q_PROPERTY(bool hasProposal READ hasProposal NOTIFY proposalChanged)
    Q_PROPERTY(QString proposalSummary READ proposalSummary NOTIFY proposalChanged)
    Q_PROPERTY(QString proposalExplanation READ proposalExplanation NOTIFY proposalChanged)
    Q_PROPERTY(QStringList proposalChanges READ proposalChanges NOTIFY proposalChanged)
    Q_PROPERTY(QStringList proposalRefused READ proposalRefused NOTIFY proposalChanged)
    Q_PROPERTY(QString answer READ answer NOTIFY proposalChanged)
    Q_PROPERTY(QVariantList history READ historyList NOTIFY historyChanged)

public:
    ShaderAssistant(ShaderStudio* studio, QObject* parent = nullptr);
    ~ShaderAssistant() override;

    bool configured() const;
    int provider() const { return static_cast<int>(config_.provider); }
    QString model() const;
    QString baseUrl() const { return QString::fromStdString(config_.baseUrl); }
    bool hasKey() const { return !key_.isEmpty(); }
    QString providerNote() const;
    bool busy() const { return reply_ != nullptr; }
    QString message() const { return message_; }
    bool messageIsError() const { return messageError_; }
    bool hasProposal() const { return proposal_ != nullptr; }
    QString proposalSummary() const { return summary_; }
    QString proposalExplanation() const { return explanation_; }
    QStringList proposalChanges() const { return changes_; }
    QStringList proposalRefused() const { return refused_; }
    QString answer() const { return answer_; }
    QVariantList historyList() const;

    // provider: 0 Claude, 1 OpenAI-compatible. An empty key keeps the saved one.
    Q_INVOKABLE void configure(int provider, const QString& model, const QString& baseUrl, const QString& key);
    Q_INVOKABLE void forgetKey();
    // mode: "generate", "refine", "animate" or "explain".
    Q_INVOKABLE void send(const QString& prompt, const QString& mode);
    Q_INVOKABLE void regenerate();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void apply();
    Q_INVOKABLE void reject();
    Q_INVOKABLE void newConversation();

signals:
    void configChanged();
    void busyChanged();
    void messageChanged();
    void proposalChanged();
    void historyChanged();

private:
    void load();
    void say(const QString& message, bool error = false);
    void finished();
    void showResult(const vfx::editor::shader::ai::Reply& reply);
    void dropProposal();

    ShaderStudio* studio_;
    vfx::editor::shader::ai::ProviderConfig config_;
    QString key_;
    QNetworkAccessManager* net_ = nullptr;
    QNetworkReply* reply_ = nullptr;

    vfx::editor::shader::ai::Mode pendingMode_ = vfx::editor::shader::ai::Mode::Generate;
    vfx::editor::shader::ai::Context pendingContext_;
    QString pendingPrompt_, lastPrompt_, lastMode_;

    std::unique_ptr<vfx::editor::shader::ai::Applied> proposal_;
    int proposalSlot_ = -1;
    QString summary_, explanation_, answer_, message_;
    QStringList changes_, refused_;
    bool messageError_ = false;
    std::vector<vfx::editor::shader::ai::Turn> history_;
};
