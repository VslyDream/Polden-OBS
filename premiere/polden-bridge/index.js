const { entrypoints, storage } = require("uxp");
const ppro = require("premierepro");
const URL = "http://127.0.0.1:37941";
const completedKey = "poldenCompletedJobs";
let polling = false;
let timer;

function status(message) {
  document.getElementById("status").textContent = message;
}

function pathKey(path) {
  return String(path || "").replace(/\\/g, "/").replace(/\/+$/, "").toLowerCase();
}

function productionRoot(footageProjectPath) {
  const parts = pathKey(footageProjectPath).split("/");
  if (parts.length < 3 || !parts[parts.length - 2].includes("footage")) {
    throw new Error("Путь Footage .prproj не находится в папке 00_Footage.");
  }
  return parts.slice(0, -2).join("/") + "/";
}

async function request(route, options = {}) {
  const key = document.getElementById("bridgeKey").value.trim();
  if (!key) throw new Error("Введите ключ Polden OBS.");
  const response = await fetch(URL + route, {
    ...options,
    headers: { "X-Polden-Key": key, "Content-Type": "application/json" },
  });
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || "Ошибка связи с Polden OBS.");
  return data;
}

async function findMedia(folder, filePath) {
  const wanted = pathKey(filePath);
  for (const item of await folder.getItems()) {
    try {
      const clip = ppro.ClipProjectItem.cast(item);
      if (clip && pathKey(await clip.getMediaFilePath()) === wanted) return clip;
    } catch (_) {
      // Bins and sequences do not have a media file path.
    }
  }
  return null;
}

async function waitForMedia(folder, filePath) {
  for (let attempt = 0; attempt < 10; attempt++) {
    const media = await findMedia(folder, filePath);
    if (media) return media;
    await new Promise(resolve => setTimeout(resolve, 300));
  }
  return null;
}

async function ensureBin(project, binPath) {
  let folder = await project.getRootItem();
  for (const name of binPath.split(/[\\/]/).filter(Boolean)) {
    let next = (await folder.getItems()).find(item => item.name === name);
    if (!next) {
      const created = project.executeTransaction(action => {
        action.addAction(folder.createBinAction(name, false));
      }, "Polden: create " + name);
      if (!created) throw new Error("Не удалось создать bin " + name);
      next = (await folder.getItems()).find(item => item.name === name);
    }
    folder = next && ppro.FolderItem.cast(next);
    if (!folder) throw new Error("Элемент " + name + " не является bin.");
  }
  return folder;
}

async function run(command) {
  const active = await ppro.Project.getActiveProject();
  if (!active || !pathKey(active.path).startsWith(productionRoot(command.projectPath))) {
    throw new Error("Откройте Production выбранной игры в Premiere и повторите операцию.");
  }
  const sequence = command.action === "timeline" ? await active.getActiveSequence() : null;
  if (command.action === "timeline" && !sequence) {
    throw new Error("В выбранной Production нет активной секвенции.");
  }
  const footageProject = pathKey(active.path) === pathKey(command.projectPath)
    ? active : await ppro.Project.open(command.projectPath);
  if (!footageProject || pathKey(footageProject.path) !== pathKey(command.projectPath)) {
    throw new Error("Не удалось открыть указанный проект Footage.");
  }
  const bin = await ensureBin(footageProject, command.binPath);
  let media = await findMedia(bin, command.filePath);
  if (!media) {
    const imported = await footageProject.importFiles([command.filePath], true, bin, false);
    if (!imported) throw new Error("Premiere не импортировал файл в bin.");
    media = await waitForMedia(bin, command.filePath);
    if (!media) throw new Error("Импорт завершился, но файл не найден в bin.");
    const saved = await footageProject.save();
    if (!saved) throw new Error("Импорт выполнен, но Footage .prproj не сохранился.");
  }
  if (command.action === "timeline") {
    const end = await sequence.getEndTime();
    const editor = ppro.SequenceEditor.getEditor(sequence);
    const inserted = active.executeTransaction(action => {
      action.addAction(editor.createInsertProjectItemAction(media, end, 0, 0, false));
    }, "Polden: add last recording");
    if (!inserted) throw new Error("Не удалось добавить запись в активную секвенцию.");
    const saved = await active.save();
    if (!saved) throw new Error("Клип добавлен, но проект с секвенцией не сохранился.");
  }
}

async function poll() {
  if (polling) return;
  polling = true;
  try {
    const command = await request("/next");
    if (!command.id) {
      status("Подключено к Polden OBS. Ожидание записи.");
      return;
    }
    const completed = JSON.parse(localStorage.getItem(completedKey) || "{}");
    const commandKey = command.id + ":" + command.action;
    let result = { id: command.id, action: command.action, ok: true };
    if (!completed[commandKey]) {
      status("Обработка " + command.filePath + "…");
      try {
        await run(command);
        completed[commandKey] = true;
        const keys = Object.keys(completed);
        for (const old of keys.slice(0, Math.max(0, keys.length - 500))) delete completed[old];
        localStorage.setItem(completedKey, JSON.stringify(completed));
      } catch (error) {
        result = { ...result, ok: false, error: String(error.message || error) };
      }
    }
    await request("/result", { method: "POST", body: JSON.stringify(result) });
    status(result.ok ? "Готово: " + command.filePath : result.error);
  } catch (error) {
    status(String(error.message || error));
  } finally {
    polling = false;
  }
}

entrypoints.setup({
  panels: {
    poldenBridge: {
      async show() {
        const key = document.getElementById("bridgeKey");
        try {
          const saved = await storage.secureStorage.getItem("poldenBridgeKey");
          if (saved) key.value = String.fromCharCode(...saved);
        } catch (error) {
          status(String(error.message || error));
        }
        document.getElementById("connect").onclick = async () => {
          try {
            if (!key.value.trim()) throw new Error("Введите ключ Polden OBS.");
            await storage.secureStorage.setItem("poldenBridgeKey", key.value.trim());
            await poll();
          } catch (error) {
            status(String(error.message || error));
          }
        };
        if (!timer) timer = setInterval(poll, 2500);
        poll();
      },
    },
  },
});
