if (typeof Module === 'undefined') {
  Module = {};
}

const ARMSX2_PERSIST_DIR = '/persist/program-cache';
const ARMSX2_LAST_PROGRAM_PATH = `${ARMSX2_PERSIST_DIR}/last-program.bin`;
const ARMSX2_LAST_PROGRAM_NAME_PATH = `${ARMSX2_PERSIST_DIR}/last-program.name`;

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
      Module.setStatus(`Loaded ${fileName} into the WASM bootstrap memory image.`);
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

Module.onRuntimeInitialized = async function () {
  const input = document.getElementById('program-file');
  const button = document.getElementById('load-program');

  if (input && button) {
    input.addEventListener('change', function () {
      armsx2SetLoadButtonEnabled(!!(input.files && input.files.length));
    });

    button.addEventListener('click', async function () {
      if (!input.files || !input.files.length) {
        Module.setStatus('Choose a PS2 ELF or PS-X EXE first.');
        return;
      }

      const file = input.files[0];
      const bytes = new Uint8Array(await file.arrayBuffer());
      await armsx2LoadProgramBytes(file.name, bytes, true);
    });
  }

  armsx2SetLoadButtonEnabled(false);
  try {
    const restored = await armsx2RestorePersistedProgram();
    if (!restored) {
      Module.setStatus('Select a PS2 ELF or PS-X EXE to inspect and map into WASM memory.');
    }
  } catch (error) {
    console.error('Failed to restore persisted executable', error);
    Module.setStatus('Executable persistence restore failed.');
    Module.setProgramInfo('No executable loaded yet.');
  }
};
