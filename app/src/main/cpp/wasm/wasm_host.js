if (typeof Module === 'undefined') {
  Module = {};
}

const ARMSX2_PERSIST_DIR = '/persist/program-cache';
const ARMSX2_LAST_PROGRAM_PATH = `${ARMSX2_PERSIST_DIR}/last-program.bin`;
const ARMSX2_LAST_PROGRAM_NAME_PATH = `${ARMSX2_PERSIST_DIR}/last-program.name`;
const ARMSX2_BROWSER_ROOT = '/browser';
const ARMSX2_BROWSER_BIOS_DIR = `${ARMSX2_BROWSER_ROOT}/bios`;
const ARMSX2_BROWSER_GAME_DIR = `${ARMSX2_BROWSER_ROOT}/games`;
// Keep in sync with s_max_browser_disc_import_bytes in wasm_main.cpp.
const ARMSX2_MAX_GAME_IMAGE_BYTES = 4700000000;

Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
  if (typeof FS === 'undefined' || typeof IDBFS === 'undefined') {
    return;
  }

  try {
    FS.mkdir('/persist');
  } catch (error) {
    if (!String(error).includes('File exists')) {
      throw error;
    }
  }

  FS.mount(IDBFS, {}, '/persist');
});

Module.syncPersist = function (populate) {
  return new Promise(function (resolve, reject) {
    if (typeof FS === 'undefined') {
      resolve();
      return;
    }

    FS.syncfs(!!populate, function (err) {
      if (err) {
        reject(err);
        return;
      }

      resolve();
    });
  });
};

Module.onExit = function () {
  if (typeof FS !== 'undefined') {
    FS.syncfs(false, function (err) {
      if (err) {
        console.error('Final IDBFS sync failed', err);
      }
    });
  }
};

Module.print = function (text) {
  console.log(text);
};

Module.printErr = function (text) {
  console.error(text);
};

function armsx2EnsurePersistDir() {
  if (typeof FS === 'undefined') {
    return;
  }

  try {
    FS.mkdir(ARMSX2_PERSIST_DIR);
  } catch (error) {
    if (!String(error).includes('File exists')) {
      throw error;
    }
  }
}

function armsx2SetLoadButtonEnabled(enabled) {
  const button = document.getElementById('load-program');
  if (button) {
    button.disabled = !enabled;
  }
}

function armsx2SetButtonEnabled(buttonId, enabled) {
  const button = document.getElementById(buttonId);
  if (button) {
    button.disabled = !enabled;
  }
}

function armsx2RenderProgramInfo(fileName, summary) {
  const header = fileName ? `Source: ${fileName}\n` : '';
  Module.setProgramInfo(`${header}${summary}`.trim());
}

function armsx2CopyToHeap(bytes) {
  const pointer = Module._malloc(bytes.length);
  Module.HEAPU8.set(bytes, pointer);
  return pointer;
}

async function armsx2PersistProgram(fileName, bytes) {
  if (typeof FS === 'undefined') {
    return;
  }

  armsx2EnsurePersistDir();
  FS.writeFile(ARMSX2_LAST_PROGRAM_PATH, bytes);
  FS.writeFile(ARMSX2_LAST_PROGRAM_NAME_PATH, fileName);
  await Module.syncPersist(false);
}

async function armsx2LoadProgramBytes(fileName, bytes, persistSelection) {
  if (!bytes || !bytes.length) {
    Module.setStatus('No executable data was provided.');
    return;
  }

  const pointer = armsx2CopyToHeap(bytes);
  try {
    Module.setStatus(`Loading ${fileName}…`);
    const success = Module._armsx2_wasm_load_program(pointer, bytes.length);
    const summaryPointer = Module._armsx2_wasm_get_program_summary();
    const summary = Module.UTF8ToString(summaryPointer);
    armsx2RenderProgramInfo(fileName, summary);

    if (success) {
      Module.setStatus(`Loaded ${fileName} into the bootstrap EE/IOP memory.`);
      if (persistSelection) {
        await armsx2PersistProgram(fileName, bytes);
      }
    } else {
      Module.setStatus(`Failed to load ${fileName}.`);
    }
  } finally {
    Module._free(pointer);
  }
}

async function armsx2RestorePersistedProgram() {
  if (typeof FS === 'undefined') {
    Module.setProgramInfo('Persistence is unavailable in this runtime.');
    return false;
  }

  await Module.syncPersist(true);
  armsx2EnsurePersistDir();

  if (!FS.analyzePath(ARMSX2_LAST_PROGRAM_PATH).exists) {
    Module.setProgramInfo('No executable loaded yet.');
    return false;
  }

  const bytes = FS.readFile(ARMSX2_LAST_PROGRAM_PATH);
  const fileName = FS.readFile(ARMSX2_LAST_PROGRAM_NAME_PATH, { encoding: 'utf8' }) || 'persisted-program.bin';
  await armsx2LoadProgramBytes(fileName, bytes, false);
  return true;
}

function armsx2RenderTextBlock(elementId, text) {
  const element = document.getElementById(elementId);
  if (element) {
    element.textContent = text;
  }
}

function armsx2EnsureDirectory(path) {
  if (typeof FS === 'undefined') {
    return;
  }

  if (!FS.analyzePath(path).exists) {
    FS.mkdir(path);
  }
}

function armsx2PrepareWorkerMount(mountPoint) {
  if (typeof FS === 'undefined') {
    throw new Error('Filesystem support is unavailable.');
  }

  armsx2EnsureDirectory(ARMSX2_BROWSER_ROOT);
  armsx2EnsureDirectory(mountPoint);

  const mountInfo = FS.analyzePath(mountPoint);
  if (mountInfo.exists && mountInfo.object && mountInfo.object.mounted) {
    FS.unmount(mountPoint);
  }
}

function armsx2ReadBrowserFileSummary() {
  const summaryPointer = Module._armsx2_wasm_get_browser_file_summary();
  return Module.UTF8ToString(summaryPointer);
}

async function armsx2MountBrowserFile(options) {
  if (typeof WORKERFS === 'undefined') {
    throw new Error('WORKERFS is unavailable in this build.');
  }

  const input = document.getElementById(options.inputId);
  if (!input || !input.files || !input.files.length) {
    Module.setStatus(options.emptyStatus);
    return;
  }

  const file = input.files[0];
  if (options.maxBytes && file.size > options.maxBytes) {
    const limitSummary = [
      'ARMSX2 WASM browser-backed file mount',
      `Result: failed`,
      `Error: ${file.name} exceeds the current 4.7 GB browser import limit.`,
      `Size: ${file.size} bytes`,
    ].join('\n');
    armsx2RenderTextBlock(options.infoId, limitSummary);
    Module.setStatus(`${file.name} is larger than the current 4.7 GB game image limit.`);
    return;
  }

  Module.setStatus(`${options.loadingLabel} ${file.name}…`);
  armsx2PrepareWorkerMount(options.mountPoint);
  FS.mount(WORKERFS, { files: [file] }, options.mountPoint);

  const mountedPath = `${options.mountPoint}/${file.name}`;
  const success = Module.ccall(options.nativeFunction, 'number', ['string'], [mountedPath]);
  armsx2RenderTextBlock(options.infoId, armsx2ReadBrowserFileSummary());
  Module.setStatus(
    success
      ? `${options.successLabel} ${file.name} using browser-backed storage.`
      : `Failed to import ${file.name}.`
  );
}

Module.onRuntimeInitialized = async function () {
  const input = document.getElementById('program-file');
  const button = document.getElementById('load-program');
  const biosInput = document.getElementById('bios-file');
  const biosButton = document.getElementById('mount-bios');
  const gameInput = document.getElementById('game-file');
  const gameButton = document.getElementById('mount-game');

  if (input && button) {
    input.addEventListener('change', function () {
      armsx2SetLoadButtonEnabled(!!(input.files && input.files.length));
    });

    button.addEventListener('click', async function () {
      if (!input.files || !input.files.length) {
        Module.setStatus('Select a PS2 ELF or PS-X EXE file first.');
        return;
      }

      const file = input.files[0];
      const bytes = new Uint8Array(await file.arrayBuffer());
      await armsx2LoadProgramBytes(file.name, bytes, true);
    });
  }

  if (biosInput && biosButton) {
    biosInput.addEventListener('change', function () {
      armsx2SetButtonEnabled('mount-bios', !!(biosInput.files && biosInput.files.length));
    });

    biosButton.addEventListener('click', async function () {
      try {
        await armsx2MountBrowserFile({
          inputId: 'bios-file',
          mountPoint: ARMSX2_BROWSER_BIOS_DIR,
          nativeFunction: 'armsx2_wasm_mount_bios',
          infoId: 'bios-info',
          emptyStatus: 'Select a PS2 BIOS image first.',
          loadingLabel: 'Mounting BIOS',
          successLabel: 'Mounted BIOS',
        });
      } catch (error) {
        console.error('Failed to mount BIOS image', error);
        Module.setStatus('BIOS mount failed.');
      }
    });
  }

  if (gameInput && gameButton) {
    gameInput.addEventListener('change', function () {
      armsx2SetButtonEnabled('mount-game', !!(gameInput.files && gameInput.files.length));
    });

    gameButton.addEventListener('click', async function () {
      try {
        await armsx2MountBrowserFile({
          inputId: 'game-file',
          mountPoint: ARMSX2_BROWSER_GAME_DIR,
          nativeFunction: 'armsx2_wasm_mount_game',
          infoId: 'game-info',
          emptyStatus: 'Select a PS2 disc image first.',
          loadingLabel: 'Importing game image',
          successLabel: 'Imported game image',
          maxBytes: ARMSX2_MAX_GAME_IMAGE_BYTES,
        });
      } catch (error) {
        console.error('Failed to import game image', error);
        Module.setStatus('Game image import failed.');
      }
    });
  }

  armsx2SetLoadButtonEnabled(false);
  armsx2SetButtonEnabled('mount-bios', false);
  armsx2SetButtonEnabled('mount-game', false);
  try {
    const restored = await armsx2RestorePersistedProgram();
    if (!restored) {
      Module.setStatus('Select a PS2 ELF or PS-X EXE to inspect and load into bootstrap EE/IOP memory.');
    }
  } catch (error) {
    console.error('Failed to restore persisted executable', error);
    Module.setStatus('Executable persistence restore failed.');
    Module.setProgramInfo('No executable loaded yet.');
  }
};
