// VFX Forge Link: a Photoshop panel that sends what you paint to VFX Forge.
//
// It saves a PNG into the VFX Forge Link folder (Documents/VFX Forge Link).
// VFX Forge watches that folder: a new picture goes onto the layer selected
// there, and sending the same name again updates every layer using it.
//
// Three ways to send:
//   - the whole picture, as Photoshop shows it (layer styles included);
//   - the selected layer (or group) alone, on the document's canvas;
//   - frames as a sprite sheet: each layer of the selected group (or each
//     top-level layer) is one frame, the bottom one first. The file is named
//     like "fire_4x2.png", so VFX Forge sets the grid on its own.

const { app, core, constants } = require("photoshop");
const { storage } = require("uxp");
const fs = storage.localFileSystem;

const TOKEN_KEY = "vfxforge-link-folder";
const MAX_SIDE = 8192;  // the largest picture VFX Forge takes

const statusLine = document.getElementById("status");
const folderName = document.getElementById("folderName");
const buttons = ["sendPicture", "sendLayer", "sendSheet"].map((id) => document.getElementById(id));

function say(text, bad) {
  statusLine.textContent = text;
  statusLine.className = bad ? "bad" : "";
}

function busy(on) {
  for (const b of buttons) {
    if (on) b.setAttribute("disabled", "");
    else b.removeAttribute("disabled");
  }
}

// A file name made from a layer or document name.
function cleanName(name, fallback) {
  let n = String(name || "").replace(/\.(psd|psb|png|jpe?g|tiff?)$/i, "");
  n = n.replace(/[\\/:*?"<>|]+/g, "-").trim();
  return n || fallback;
}

// The folder to send to, chosen once and remembered.
async function linkFolder(choose) {
  const token = localStorage.getItem(TOKEN_KEY);
  if (token && !choose) {
    try {
      const folder = await fs.getEntryForPersistentToken(token);
      folderName.textContent = folder.nativePath || folder.name;
      return folder;
    } catch (e) {
      // The folder moved or the permission lapsed: ask again.
    }
  }
  say("Choose the folder VFX Forge watches: Documents › VFX Forge Link.");
  const folder = await fs.getFolder({ initialDomain: storage.domains.userDocuments });
  if (!folder) {
    return null;
  }
  localStorage.setItem(TOKEN_KEY, await fs.createPersistentToken(folder));
  folderName.textContent = folder.nativePath || folder.name;
  if (folder.name !== "VFX Forge Link") {
    say(`Sending to "${folder.name}". VFX Forge watches Documents › VFX Forge Link, so choose that one unless you moved it.`);
  } else {
    say("Ready.");
  }
  return folder;
}

// A new see-through document the size given, made active.
async function blankDocument(width, height, name) {
  const doc = await app.documents.add({
    width,
    height,
    resolution: 72,
    mode: constants.NewDocumentMode.RGB,
    fill: constants.DocumentFill.TRANSPARENT,
    name,
  });
  return doc;
}

// Copies a layer (or group) of one document into another, and returns the copy.
async function copyInto(layer, from, into) {
  app.activeDocument = from;
  const before = new Set(into.layers.map((l) => l.id));
  await layer.duplicate(into);
  app.activeDocument = into;
  const copy = into.layers.find((l) => !before.has(l.id));
  if (!copy) {
    throw new Error(`Couldn't copy "${layer.name}".`);
  }
  copy.visible = true;
  return copy;
}

// Removes the empty layer a new document starts with.
async function dropStarterLayers(doc, keep) {
  for (const l of [...doc.layers]) {
    if (!keep.has(l.id)) {
      await l.delete();
    }
  }
}

async function savePng(doc, folder, fileName) {
  const file = await folder.createFile(fileName, { overwrite: true });
  await doc.saveAs.png(file, { compression: 6 }, true);
  return file;
}

// Frames laid out with no empty cells when the count allows it.
function sheetGrid(count, width, height) {
  const square = Math.ceil(Math.sqrt(count));
  for (let columns = square; columns <= count; columns++) {
    if (count % columns === 0 && columns * width <= MAX_SIDE && (count / columns) * height <= MAX_SIDE) {
      return { columns, rows: count / columns };
    }
  }
  return { columns: square, rows: Math.ceil(count / square) };
}

async function run(name, work) {
  const doc = app.activeDocument;
  if (!doc) {
    say("Open a picture in Photoshop first.", true);
    return;
  }
  const folder = await linkFolder(false);
  if (!folder) {
    say("No folder chosen, so nothing was sent.", true);
    return;
  }
  busy(true);
  try {
    let said = "";
    await core.executeAsModal(async () => {
      said = await work(doc, folder);
    }, { commandName: name });
    app.activeDocument = doc;
    say(said);
  } catch (e) {
    say(`Couldn't send: ${e.message || e}`, true);
  } finally {
    busy(false);
  }
}

// ---- the three ways to send

async function sendPicture(doc, folder) {
  const fileName = `${cleanName(doc.name, "Picture")}.png`;
  await savePng(doc, folder, fileName);
  return `Sent ${fileName}.`;
}

async function sendLayer(doc, folder) {
  const layer = doc.activeLayers[0];
  if (!layer) {
    throw new Error("Select a layer first.");
  }
  const width = Math.round(doc.width);
  const height = Math.round(doc.height);
  const fileName = `${cleanName(layer.name, "Layer")}.png`;
  const out = await blankDocument(width, height, "VFX Forge send");
  try {
    const copy = await copyInto(layer, doc, out);
    await dropStarterLayers(out, new Set([copy.id]));
    await savePng(out, folder, fileName);
  } finally {
    await out.closeWithoutSaving();
  }
  return `Sent the layer as ${fileName}.`;
}

async function sendSheet(doc, folder) {
  const selected = doc.activeLayers[0];
  const group = selected && selected.kind === constants.LayerKind.GROUP ? selected : null;
  // Layers are listed top first; frames run from the bottom up.
  const frames = [...(group ? group.layers : doc.layers)].reverse();
  if (frames.length < 2) {
    throw new Error("A sprite sheet needs at least two frame layers: select the group they are in.");
  }
  const width = Math.round(doc.width);
  const height = Math.round(doc.height);
  const { columns, rows } = sheetGrid(frames.length, width, height);
  if (columns * width > MAX_SIDE || rows * height > MAX_SIDE) {
    throw new Error(`${frames.length} frames of ${width} × ${height} make a sheet larger than ${MAX_SIDE} pixels. Make the canvas smaller (Image › Image Size) and send again.`);
  }
  const base = cleanName(group ? group.name : doc.name, "Frames");
  const fileName = `${base}_${columns}x${rows}.png`;
  const sheet = await blankDocument(columns * width, rows * height, "VFX Forge sheet");
  try {
    const keep = new Set();
    for (let i = 0; i < frames.length; i++) {
      const copy = await copyInto(frames[i], doc, sheet);
      keep.add(copy.id);
      const x = (i % columns) * width;
      const y = Math.floor(i / columns) * height;
      if (x || y) {
        await copy.translate(x, y);
      }
    }
    await dropStarterLayers(sheet, keep);
    await savePng(sheet, folder, fileName);
  } finally {
    await sheet.closeWithoutSaving();
  }
  const empty = columns * rows - frames.length;
  return empty
    ? `Sent ${frames.length} frames as ${fileName}. In VFX Forge, set Use to ${frames.length} so the ${empty} empty cell${empty > 1 ? "s" : ""} don't play.`
    : `Sent ${frames.length} frames as ${fileName}.`;
}

document.getElementById("sendPicture").addEventListener("click", () => run("Send to VFX Forge", sendPicture));
document.getElementById("sendLayer").addEventListener("click", () => run("Send layer to VFX Forge", sendLayer));
document.getElementById("sendSheet").addEventListener("click", () => run("Send sprite sheet to VFX Forge", sendSheet));
document.getElementById("changeFolder").addEventListener("click", () => linkFolder(true));

// Show the remembered folder, if there is one, without asking.
(async () => {
  const token = localStorage.getItem(TOKEN_KEY);
  if (!token) {
    say("The first send asks once for the folder VFX Forge watches.");
    return;
  }
  try {
    const folder = await fs.getEntryForPersistentToken(token);
    folderName.textContent = folder.nativePath || folder.name;
    say("Ready.");
  } catch (e) {
    say("The first send asks for the folder VFX Forge watches.");
  }
})();
