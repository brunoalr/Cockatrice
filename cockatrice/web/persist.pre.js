if (!ENVIRONMENT_IS_PTHREAD) {
  (Module.preRun ??= []).push(() => {
    // IDBFS names its database after the mount point, and IndexedDB is shared by every page of the origin
    const mount = '/cockatrice-data';
    const dataRoot = `${mount}/Cockatrice/Cockatrice/`;
    let line;
    let inMemory = false;
    const say = text => {
      if (line?.textContent === text)
        return;
      if (!line) {
        line = document.createElement('p');
        line.style.cssText = 'margin: 0; padding: 0 8px; height: 24px; font: 13px/24px sans-serif; ' +
          'white-space: nowrap; overflow: hidden; text-overflow: ellipsis';
        const app = Module.qtContainerElements?.[0];
        if (app) {
          app.style.height = 'calc(100% - 24px)';
          app.after(line);
        } else {
          document.body.append(line);
        }
      }
      line.textContent = line.title = text;
      console.warn(text);
    };
    const mayLose = () => inMemory || say('The browser may delete saved decks: export the decks you want to keep.');
    const keepInMemory = () => {
      inMemory = true;
      say('This browser does not let the page store data: decks and settings are lost when the page closes. ' +
          'Export the decks you want to keep.');
      removeRunDependency(mount);
    };

    FS.mkdir(mount);
    FS.mount(IDBFS, {}, mount);
    ENV.XDG_DATA_HOME = mount;
    addRunDependency(mount);
    if (!navigator.locks)
      return keepInMemory();
    // a reloading page may not have released the lock yet
    const wait = new AbortController();
    setTimeout(() => wait.abort(), 3000);
    navigator.locks.request('cockatrice-data', { signal: wait.signal }, () => new Promise(() => FS.syncfs(true, error => {
      if (error)
        return keepInMemory();
      let last = Promise.resolve();
      let queued = null;
      const flush = () => queued ??= last.catch(() => {}).then(() => {
        queued = null;
        return last = new Promise((resolve, reject) =>
          FS.syncfs(false, error => error ? (mayLose(), reject(error)) : resolve()));
      });
      const walk = dir => FS.readdir(dir).filter(name => name !== '.' && name !== '..').flatMap(name =>
        FS.isDir(FS.stat(`${dir}/${name}`).mode) ? walk(`${dir}/${name}`) : [`${dir}/${name}`.replace(dataRoot, '')]);
      globalThis.cockatricePersist = { flush, files: () => walk(mount) };

      const flushQuietly = () => flush().catch(() => {});
      setInterval(flushQuietly, 5000);
      document.addEventListener('visibilitychange', () => document.visibilityState === 'hidden' && flushQuietly());
      addEventListener('pagehide', flushQuietly);
      Promise.resolve(navigator.storage?.persist?.()).then(granted => granted || mayLose(), mayLose);
      removeRunDependency(mount);
    }))).catch(error => error?.name === 'AbortError'
      ? say('Cockatrice is already open in another tab of this browser. Close that tab, then reload this page.')
      : keepInMemory());
  });
}
