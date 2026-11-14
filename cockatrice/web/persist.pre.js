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
  // IDBFS names its database after the mount point, and IndexedDB is shared by every page of the origin
  const mount = '/cockatrice-data';
  const dataRoot = `${mount}/Cockatrice/Cockatrice/`;
  const keepCopy = 'To keep a copy of a deck, use Deck Editor > Save deck to clipboard and paste it outside the browser.';
  const noStorage = `This browser does not let the page store data: decks and settings last only until the page closes. ${keepCopy}`;
  const loadFailed = 'The decks and settings stored in this browser could not be loaded: ' +
                     `this session starts without them and is not saved. ${keepCopy}`;
  const writeFailed = 'Recent changes could not be stored in this browser and are lost if the page closes; ' +
                      'Cockatrice keeps trying.';
  const mayBeDeleted = `This browser may delete the decks and settings saved here. ${keepCopy}`;
  const refusal = 'Cockatrice is already open in another tab of this browser. Close that tab, then reload this page.';

  // a live region announces only changes made after it is in the page, so the line exists from the start
  const line = document.createElement('p');
  let failing = false;
  let mayDelete = false;
  let asked = false;
  let last = Promise.resolve();
  let queued = null;

  const say = text => {
    if (line.textContent === text)
      return;
    line.textContent = text;
    if (text)
      console.warn(text);
  };

  const show = () => {
    if (failing)
      say(writeFailed);
    else if (mayDelete)
      say(mayBeDeleted);
    else
      say('');
  };

  const keepInMemory = text => {
    say(text);
    removeRunDependency(mount);
  };

  const syncToStorage = (resolve, reject) => {
    FS.syncfs(false, error => {
      failing = Boolean(error);
      if (error) {
        // a connection the browser closed fails every later write: open a new one next time
        IDBFS.dbs[mount]?.close();
        delete IDBFS.dbs[mount];
      }
      show();
      if (error)
        reject(error);
      else
        resolve();
    });
  };

  const writeOnce = () => {
    queued = null;
    last = new Promise(syncToStorage);
    return last;
  };

  const flush = () => {
    queued ??= last.catch(() => {}).then(writeOnce);
    return queued;
  };

  const flushQuietly = () => {
    flush().catch(() => {});
  };

  const flushWhenHidden = () => {
    if (document.visibilityState === 'hidden')
      flushQuietly();
  };

  const recordPersistence = granted => {
    mayDelete = !granted;
    show();
  };

  const flushPlayerData = () => {
    if (!asked) {
      asked = true;
      Promise.resolve(navigator.storage?.persist?.()).then(recordPersistence, () => recordPersistence(false));
    }
    return flush();
  };

  const walk = dir => FS.readdir(dir).filter(name => name !== '.' && name !== '..').flatMap(name => {
    const path = `${dir}/${name}`;
    return FS.isDir(FS.stat(path).mode) ? walk(path) : [path.replace(dataRoot, '')];
  });

  const loaded = error => {
    if (error) {
      keepInMemory(loadFailed);
      return;
    }
    globalThis.cockatricePersist = { flush, flushPlayerData, files: () => walk(mount) };
    setInterval(flushQuietly, 5000);
    document.addEventListener('visibilitychange', flushWhenHidden);
    addEventListener('pagehide', flushQuietly);
    removeRunDependency(mount);
  };

  const holdData = () => new Promise(() => {
    FS.syncfs(true, loaded);
  });

  const lockFailed = error => {
    if (error?.name !== 'AbortError') {
      keepInMemory(noStorage);
      return;
    }
    const message = document.querySelector('#message');
    if (message)
      message.textContent = refusal;
    else
      say(refusal);
  };

  const start = () => {
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

    FS.mkdir(mount);
    FS.mount(IDBFS, {}, mount);
    ENV.XDG_DATA_HOME = mount;
    addRunDependency(mount);
    if (!navigator.locks || typeof indexedDB === 'undefined') {
      keepInMemory(noStorage);
      return;
    }
    // a reloading page may not have released the lock yet
    const wait = new AbortController();
    setTimeout(() => wait.abort(), 3000);
    navigator.locks.request('cockatrice-data', { signal: wait.signal }, holdData).catch(lockFailed);
  };

  Module.preRun ??= [];
  Module.preRun.push(start);
}
