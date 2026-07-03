if (typeof Module === 'undefined') {
  Module = {};
}

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
  FS.syncfs(true, function (err) {
    if (err) {
      console.error('Initial IDBFS sync failed', err);
    }
  });
});

Module.onExit = function () {
  if (typeof FS !== 'undefined') {
    FS.syncfs(false, function () {});
  }
};

Module.print = function (text) {
  console.log(text);
};

Module.printErr = function (text) {
  console.error(text);
};
