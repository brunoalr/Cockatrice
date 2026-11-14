// usage: node --experimental-websocket wasm_open_connect.mjs <devtools port> <seconds>
const [, , port, seconds] = process.argv
setTimeout(() => {
  console.log('the page stopped answering')
  process.exit(1)
}, seconds * 2000)
const page = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find(target => target.type === 'page')
const socket = new WebSocket(page.webSocketDebuggerUrl)
await new Promise(resolve => (socket.onopen = resolve))
const replies = new Map()
socket.onmessage = event => {
  const message = JSON.parse(event.data)
  replies.get(message.id)?.(message.result)
}
let lastId = 0
const send = (method, params = {}) =>
  new Promise(resolve => {
    replies.set(++lastId, resolve)
    socket.send(JSON.stringify({ id: lastId, method, params }))
  })
const evaluate = async expression => (await send('Runtime.evaluate', { expression, returnByValue: true })).result.value
const screen = "document.querySelector('#qt-shadow-container').shadowRoot"

// Qt takes keys from the focused window's input element, and reads Command as Ctrl on a Mac.
const mac = await evaluate(`${screen}.querySelector('.qt-window-input-element').focus(), navigator.platform.startsWith('Mac')`)
for (const type of ['rawKeyDown', 'keyUp']) {
  await send('Input.dispatchKeyEvent', { type, modifiers: mac ? 4 : 2, key: 'l', code: 'KeyL', windowsVirtualKeyCode: 76 })
}

for (let tick = 0; tick < seconds * 4; tick++) {
  const titles = await evaluate(`[...${screen}.querySelectorAll('.window-name')].map(name => name.textContent)`)
  if (titles.includes('Connect to Server')) {
    console.log('the connect dialog opened')
    process.exit(0)
  }
  await new Promise(resolve => setTimeout(resolve, 250))
}
console.log('the connect dialog did not open')
process.exit(1)
