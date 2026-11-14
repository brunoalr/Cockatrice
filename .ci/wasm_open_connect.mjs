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

// Qt mirrors its widgets into the page only once a screen reader is asked for
await evaluate(`${screen}.querySelectorAll('button.hidden-visually-read-by-screen-reader').forEach(b => b.click())`)
// Qt takes keys from the focused window's input element, and reads Command as Ctrl on a Mac.
const mac = await evaluate(`${screen}.querySelector('.qt-window-input-element').focus(), navigator.platform.startsWith('Mac')`)
for (const type of ['rawKeyDown', 'keyUp']) {
  await send('Input.dispatchKeyEvent', { type, modifiers: mac ? 4 : 2, key: 'l', code: 'KeyL', windowsVirtualKeyCode: 76 })
}

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))
const opened = async () => {
  for (let tick = 0; tick < seconds * 4; tick++) {
    const titles = await evaluate(`[...${screen}.querySelectorAll('.window-name')].map(name => name.textContent)`)
    if (titles.includes('Connect to Server')) {
      return true
    }
    await sleep(250)
  }
  return false
}
if (!(await opened())) {
  console.log('the connect dialog did not open')
  process.exit(1)
}
console.log('the connect dialog opened')

// the player name field is the text field just before the password field
await send('Input.insertText', { text: 'abc' })
const fields = `(inputs => inputs.slice(inputs.findIndex(e => e.type === 'password') - 1).map(e => e.value))
  ([...${screen}.querySelectorAll('.qt-window-a11y-container input')].filter(e => e.type === 'text' || e.type === 'password'))`
let player, password
for (let tick = 0; tick < 20 && !player && !password; tick++) {
  await sleep(250);
  [player, password] = await evaluate(fields)
}
if (player !== 'abc' || password) {
  console.log(`typing in the connect dialog went elsewhere: player name "${player}", password of ${password?.length} characters`)
  process.exit(1)
}
process.exit(0)
