// usage: node --experimental-websocket wasm_persist.mjs <devtools port> <seconds>
// Runs against a page that has started; checks that its data root survives reloads.
const [, , port, seconds] = process.argv
const devtools = `http://127.0.0.1:${port}`
const database = '/cockatrice-data'
const root = `${database}/Cockatrice/Cockatrice/`
const started = 'ui.show() finished'
const refusal = 'already open in another tab'
const writeFailed = 'could not be stored'
const noStorage = 'does not let the page store data'
const loadFailed = 'could not be loaded'
const mayDelete = text => text.includes('may delete') && text.includes('Deck Editor > Save deck to clipboard')
const deck = 'decks/persist-smoke.cod'
const screen = "document.querySelector('#qt-shadow-container').shadowRoot"
const statusLine = "[...document.querySelectorAll('[role=status]')].map(e => e.textContent).join(' ')"

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))
const until = async (what, test, ms = seconds * 1000) => {
  for (const end = Date.now() + ms; Date.now() < end; await sleep(250)) {
    if (await test()) {
      return
    }
  }
  throw new Error(`${what} within ${ms / 1000} s`)
}

const attach = async target => {
  const socket = new WebSocket(target.webSocketDebuggerUrl)
  await new Promise((resolve, reject) => {
    socket.onopen = resolve
    socket.onerror = reject
  })
  const replies = new Map()
  const logged = []
  socket.onmessage = event => {
    const message = JSON.parse(event.data)
    if (message.id) {
      replies.get(message.id)?.(message)
      replies.delete(message.id)
    } else if (message.method === 'Runtime.consoleAPICalled') {
      logged.push(message.params.args.map(arg => String(arg.value)).join(' '))
    }
  }
  socket.onclose = () => {
    for (const reply of replies.values()) {
      reply({ error: { message: `the tab ${target.id} closed` } })
    }
    replies.clear()
  }
  const count = text => logged.filter(line => line.includes(text)).length
  let lastId = 0
  const send = (method, params = {}) =>
    new Promise((resolve, reject) => {
      if (socket.readyState !== WebSocket.OPEN) {
        throw new Error(`the tab ${target.id} is closed`)
      }
      replies.set(++lastId, message => (message.error ? reject(new Error(message.error.message)) : resolve(message.result)))
      socket.send(JSON.stringify({ id: lastId, method, params }))
    })
  const evaluate = async expression => {
    const { result, exceptionDetails } = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })
    if (exceptionDetails) {
      throw new Error(exceptionDetails.exception?.description?.split('\n')[0] ?? exceptionDetails.text)
    }
    return result.value
  }
  await send('Runtime.enable')
  return {
    target,
    send,
    evaluate,
    count,
    startups: () => count(started),
    // the old document keeps answering until the new one commits
    reload: async () => {
      const before = count(started)
      await evaluate('window.persistSmokeOldDocument = true')
      await send('Page.reload')
      await until('no new document', async () => !(await evaluate('window.persistSmokeOldDocument ?? false').catch(() => true)))
      return before
    },
    started: before => until('no start-up', () => count(started) > before),
  }
}

const pages = async () => (await (await fetch(`${devtools}/json`)).json()).filter(target => target.type === 'page')

const newTab = async (url, script) => {
  const tab = await attach(await (await fetch(`${devtools}/json/new?about:blank`, { method: 'PUT' })).json())
  if (script) {
    await tab.send('Page.enable')
    await tab.send('Page.addScriptToEvaluateOnNewDocument', { source: script })
  }
  await tab.send('Page.navigate', { url })
  return tab
}

const closeTab = async tab => {
  await fetch(`${devtools}/json/close/${tab.target.id}`)
  await until('a tab did not close', async () => !(await pages()).some(target => target.id === tab.target.id))
}

// IDBFS keeps one record per path: { timestamp, mode, contents }
const readStore = page =>
  page.evaluate(`new Promise((resolve, reject) => {
    const open = indexedDB.open(${JSON.stringify(database)})
    open.onerror = () => reject(open.error)
    open.onsuccess = () => {
      const db = open.result
      const store = db.transaction('FILE_DATA').objectStore('FILE_DATA')
      const keys = store.getAllKeys()
      const values = store.getAll()
      values.onsuccess = () => {
        db.close()
        const files = {}
        keys.result.forEach((key, i) => {
          const { timestamp, contents } = values.result[i]
          if (!contents || !key.startsWith(${JSON.stringify(root)}))
            return
          let text = null
          try {
            if (/\\.(ini|cod)$/.test(key))
              text = new TextDecoder('utf-8', { fatal: true }).decode(contents)
          } catch {}
          files[key.slice(${root.length})] = { time: new Date(timestamp).getTime(), size: contents.length, text }
        })
        resolve(files)
      }
      values.onerror = () => reject(values.error)
    }
  })`)

const clientId = async page => {
  const network = (await readStore(page))['settings/network.ini']
  const id = network?.text?.match(/^clientId=(.*)$/m)?.[1]
  if (!id || id === 'notset') {
    throw new Error(`no client id in the stored settings/network.ini: ${JSON.stringify(network?.text)}`)
  }
  return id
}

const brokenIni = files =>
  Object.entries(files)
    .filter(([path]) => path.endsWith('.ini'))
    .filter(([, { text }]) => text === null || !text.endsWith('\n') ||
            !text.split('\n').slice(0, -1).every(line => /^(\[.*\]|[^=[;#][^=]*=.*|[;#].*|\s*)$/.test(line)))
    .map(([path]) => path)

const control = (role, name) =>
  `[...${screen}.querySelectorAll('.qt-window-a11y-container ${role}')].find(e => (e.getAttribute('aria-label') || e.textContent) === ${JSON.stringify(name)})`

const openDeck = async page => {
  // Qt mirrors its widgets into the page only once a screen reader is asked for
  await page.evaluate(`${screen}.querySelectorAll('button.hidden-visually-read-by-screen-reader').forEach(b => b.click())`)
  await until('no Deck Storage tab', () => page.evaluate(`!!${control('[role=tab]', 'Deck Storage')}`), 10000)
  await page.evaluate(`${control('[role=tab]', 'Deck Storage')}.click()`)
  const open = control('button', 'Open in deck editor')
  await until('no deck storage toolbar', () => page.evaluate(`${open}?.getBoundingClientRect().height > 0`), 10000)
  const bar = await page.evaluate(`(r => ({ x: r.x, y: r.y }))(${open}.getBoundingClientRect())`)
  // the local deck list sits above its toolbar; its first row takes the Down key
  for (const type of ['mousePressed', 'mouseReleased']) {
    await page.send('Input.dispatchMouseEvent', { type, x: bar.x + 10, y: bar.y - 40, button: 'left', clickCount: 1 })
  }
  for (const type of ['rawKeyDown', 'keyUp']) {
    await page.send('Input.dispatchKeyEvent', { type, key: 'ArrowDown', code: 'ArrowDown', windowsVirtualKeyCode: 40 })
  }
  const loads = page.count(deck)
  await page.evaluate(`${open}.click()`)
  await until('the deck did not open', () => page.count(deck) > loads, 10000)
  await sleep(1000)
}

const saveDeck = async page => {
  const mac = await page.evaluate(`${screen}.querySelector('.qt-window-input-element').focus(), navigator.platform.startsWith('Mac')`)
  for (const type of ['rawKeyDown', 'keyUp']) {
    await page.send('Input.dispatchKeyEvent', { type, modifiers: mac ? 4 : 2, key: 's', code: 'KeyS', windowsVirtualKeyCode: 83 })
  }
}

const checks = {
  async a(state) {
    const files = await state.page.evaluate('cockatricePersist.files()')
    console.log(`files after start-up: ${files.join(' ')}`)
    if (!files.includes('settings/network.ini')) {
      throw new Error('settings/network.ini is not in the data root')
    }
  },

  async b(state) {
    await state.page.evaluate('cockatricePersist.flush()')
    const before = await clientId(state.page)
    await state.page.started(await state.page.reload())
    await state.page.evaluate('cockatricePersist.flush()')
    const after = await clientId(state.page)
    if (after !== before) {
      throw new Error(`the client id was ${before} before the reload and ${after} after it`)
    }
    console.log(`files after a second start: ${(await state.page.evaluate('cockatricePersist.files()')).join(' ')}`)
    console.log(`settings/paths.ini after a second start:\n${(await readStore(state.page))['settings/paths.ini']?.text}`)
  },

  async c(state) {
    const first = state.page
    const second = await newTab(state.url)
    await until(`the second tab did not say "${refusal}"`,
                () => second.evaluate(`!!document.body?.innerText.includes(${JSON.stringify(refusal)})`))
    const keptMessage = await second.evaluate(`document.querySelector('#message')?.textContent.includes(${JSON.stringify(refusal)}) === false`)
    await sleep(5000)
    if (second.startups()) {
      throw new Error('the second tab started')
    }
    await closeTab(first)
    state.page = second
    // nothing holds the data root now, so nothing writes over this deck
    await second.evaluate(`new Promise((resolve, reject) => {
      const open = indexedDB.open(${JSON.stringify(database)})
      open.onsuccess = () => {
        const store = open.result.transaction('FILE_DATA', 'readwrite').objectStore('FILE_DATA')
        const contents = new TextEncoder().encode('<?xml version="1.0" encoding="UTF-8"?>\\n<cockatrice_deck version="1">\\n' +
          '    <deckname>persist smoke</deckname>\\n    <comments></comments>\\n</cockatrice_deck>\\n')
        store.put({ timestamp: new Date(1000), mode: 0o100644, contents }, ${JSON.stringify(root + deck)})
        store.transaction.oncomplete = () => (open.result.close(), resolve())
        store.transaction.onerror = () => reject(store.transaction.error)
      }
      open.onerror = () => reject(open.error)
    })`)
    await second.started(await second.reload())
    if (keptMessage) {
      throw new Error('the refused tab kept its loading message instead of the refusal')
    }
  },

  async '3a'(state) {
    const page = state.page
    await openDeck(page)
    const line = await page.evaluate(statusLine)
    if (line) {
      throw new Error(`a line shows before anything was saved: ${line}`)
    }
    const before = (await readStore(page))[deck]?.time
    await page.evaluate(`{
      const flushPlayerData = cockatricePersist.flushPlayerData
      cockatricePersist.flushPlayerData = () => (window.deckFlush = flushPlayerData())
    }`)
    await saveDeck(page)
    await until('saving the deck did not write to storage at once', () => page.evaluate('!!window.deckFlush'), 3000)
    await page.evaluate('window.deckFlush')
    // headless Chrome refuses persistent storage to a site nobody has used
    await until('no line says how to keep a deck', async () => mayDelete(await page.evaluate(statusLine)), 5000)
    const restart = await page.reload()
    const after = (await readStore(page))[deck]?.time
    if (!(after > before)) {
      throw new Error(`the stored deck is from ${before} before the save and ${after} after it`)
    }
    await page.started(restart)
  },

  async e(state) {
    const page = state.page
    await openDeck(page)
    const before = (await readStore(page))[deck].time
    // closes the client's connection, as a browser does when it drops a page's storage
    await page.evaluate(`window.transaction = IDBDatabase.prototype.transaction,
      IDBDatabase.prototype.transaction = function () {
        this.close()
        throw new DOMException('closed by the check', 'InvalidStateError')
      }`)
    await saveDeck(page)
    await until('no line says the write failed', async () => (await page.evaluate(statusLine)).includes(writeFailed), 10000)
    await page.evaluate('IDBDatabase.prototype.transaction = window.transaction')
    await page.evaluate('cockatricePersist.flush()')
    const line = await page.evaluate(statusLine)
    if (line.includes(writeFailed)) {
      throw new Error(`a write succeeded and the line still says: ${line}`)
    }
    const restart = await page.reload()
    const after = (await readStore(page))[deck].time
    if (!(after > before)) {
      throw new Error(`the deck saved while writes failed is not in storage: ${before} before, ${after} after`)
    }
    await page.started(restart)
  },

  async f(state) {
    const noLocks = await newTab(state.url, "Object.defineProperty(Navigator.prototype, 'locks', { get: () => undefined })")
    await noLocks.started(0)
    const memoryLine = await noLocks.evaluate(statusLine)
    await closeTab(noLocks)
    if (!memoryLine.includes(noStorage)) {
      throw new Error(`without Web Locks the line says: ${memoryLine}`)
    }
    await closeTab(state.page)
    state.page = await newTab(state.url, "IDBFactory.prototype.open = () => { throw new DOMException('refused by the check', 'UnknownError') }")
    await state.page.started(0)
    const failedLine = await state.page.evaluate(statusLine)
    if (!failedLine.includes(loadFailed)) {
      throw new Error(`when the stored data cannot be loaded the line says: ${failedLine}`)
    }
  },
}

setTimeout(() => {
  console.log('the persistence checks stopped answering')
  process.exit(1)
}, seconds * 10000)
const [first] = await pages()
const state = { page: await attach(first), url: first.url }
let failed = false
for (const [name, check] of Object.entries(checks)) {
  try {
    await check(state)
    console.log(`persistence check (${name}) passed`)
  } catch (e) {
    console.log(`persistence check (${name}) FAILED: ${e.message}`)
    failed = true
  }
}
process.exit(failed ? 1 : 0)
