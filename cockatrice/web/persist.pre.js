/***************************************************************************
 *   Copyright (C) 2026 by Bruno Alexandre Rosa                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 ***************************************************************************/

if (!ENVIRONMENT_IS_PTHREAD) {
  (Module.preRun ??= []).push(() => {
    // IDBFS names its database after the mount point, and IndexedDB is shared by every page of the origin
    const mount = '/cockatrice-data';
    const dataRoot = `${mount}/Cockatrice/Cockatrice/`;
    const keepCopy = 'To keep a copy of a deck, use Deck Editor > Save deck to clipboard and paste it outside the browser.';
    const noStorage = `This browser does not let the page store data: decks and settings last only until the page closes. ${keepCopy}`;

    // a live region announces only changes made after it is in the page, so the line exists from the start
    const line = document.createElement('p');
    line.setAttribute('role', 'status');
    line.style.cssText = 'margin: 0 8px; font: 13px/1.6 sans-serif';
    const app = Module.qtContainerElements?.[0];
    if (app) {
      Object.assign(document.body.style, { display: 'flex', flexDirection: 'column' });
      Object.assign(app.style, { flex: '1', minHeight: '0' });
      app.after(line);
    } else {
      document.body.append(line);
    }
    const say = text => {
      if (line.textContent === text)
        return;
      line.textContent = text;
      if (text)
        console.warn(text);
    };
    const keepInMemory = text => {
      say(text);
      removeRunDependency(mount);
    };

    FS.mkdir(mount);
    FS.mount(IDBFS, {}, mount);
    ENV.XDG_DATA_HOME = mount;
    addRunDependency(mount);
    if (!navigator.locks || typeof indexedDB == 'undefined')
      return keepInMemory(noStorage);
    // a reloading page may not have released the lock yet
    const wait = new AbortController();
    setTimeout(() => wait.abort(), 3000);
    navigator.locks.request('cockatrice-data', { signal: wait.signal }, () => new Promise(() => FS.syncfs(true, error => {
      if (error)
        return keepInMemory('The decks and settings stored in this browser could not be loaded: ' +
                            `this session starts without them and is not saved. ${keepCopy}`);
      let failing = false;
      let mayDelete = false;
      const show = () => say(failing ? 'Recent changes could not be stored in this browser and are lost if the page closes; ' +
                                       'Cockatrice keeps trying.'
                                     : mayDelete ? `This browser may delete the decks and settings saved here. ${keepCopy}` : '');
      let last = Promise.resolve();
      let queued = null;
      const flush = () => queued ??= last.catch(() => {}).then(() => {
        queued = null;
        return last = new Promise((resolve, reject) => FS.syncfs(false, error => {
          failing = !!error;
          if (error) {
            // a connection the browser closed fails every later write: open a new one next time
            IDBFS.dbs[mount]?.close();
            delete IDBFS.dbs[mount];
          }
          show();
          error ? reject(error) : resolve();
        }));
      });
      let asked = false;
      const flushPlayerData = () => {
        if (!asked) {
          asked = true;
          Promise.resolve(navigator.storage?.persist?.()).catch(() => false).then(granted => {
            mayDelete = !granted;
            show();
          });
        }
        return flush();
      };
      const walk = dir => FS.readdir(dir).filter(name => name !== '.' && name !== '..').flatMap(name =>
        FS.isDir(FS.stat(`${dir}/${name}`).mode) ? walk(`${dir}/${name}`) : [`${dir}/${name}`.replace(dataRoot, '')]);
      globalThis.cockatricePersist = { flush, flushPlayerData, files: () => walk(mount) };

      const flushQuietly = () => flush().catch(() => {});
      setInterval(flushQuietly, 5000);
      document.addEventListener('visibilitychange', () => document.visibilityState === 'hidden' && flushQuietly());
      addEventListener('pagehide', flushQuietly);
      removeRunDependency(mount);
    }))).catch(error => {
      if (error?.name !== 'AbortError')
        return keepInMemory(noStorage);
      const refusal = 'Cockatrice is already open in another tab of this browser. Close that tab, then reload this page.';
      const message = document.querySelector('#message');
      if (message)
        message.textContent = refusal;
      else
        say(refusal);
    });
  });
}
