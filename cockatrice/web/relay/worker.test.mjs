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

import { test } from 'node:test'
import assert from 'node:assert/strict'
import { handle } from './worker.mjs'

const origin = 'https://example.github.io'
const env = { ALLOWED_ORIGINS: origin }
const relay = 'https://relay.test'
const spellbook = '/backend.commanderspellbook.com/estimate-bracket'
const image = '/card-images.archidekt.com/a/b.png'
const json = { method: 'POST', headers: { origin, 'content-type': 'application/json' }, body: '{"main":"1 Sol Ring"}' }

const request = (path, init = {}) => new Request(relay + path, { duplex: 'half', ...init, headers: init.headers ?? { origin } })

async function send(path, init, answer = () => new Response('upstream body'), environment = env) {
  const calls = []
  const upstreamFetch = async (url, options) => {
    calls.push({ url, ...options, headers: [...new Headers(options.headers)] })
    return answer()
  }
  const response = await handle(request(path, init), environment, upstreamFetch)
  return { response, calls }
}

function streamOf(size) {
  let left = size
  return new ReadableStream({
    pull(controller) {
      const chunk = Math.min(65536, left)
      controller.enqueue(new Uint8Array(chunk).fill(32))
      left -= chunk
      if (left === 0) controller.close()
    },
  })
}

function assertCors(response) {
  assert.equal(response.headers.get('access-control-allow-origin'), origin)
  assert.equal(response.headers.get('vary'), 'Origin')
}

test('every table row is forwarded to its upstream URL with its method', async () => {
  const rows = [
    ['GET', '/archidekt.com/api/decks/v3/', 'https://archidekt.com/api/decks/v3/'],
    ['GET', '/archidekt.com/api/decks/v3/?name=Atraxa&page=2', 'https://archidekt.com/api/decks/v3/?name=Atraxa&page=2'],
    ['GET', '/archidekt.com/api/decks/123456/', 'https://archidekt.com/api/decks/123456/'],
    ['GET', '/archidekt.com/api/decks/123456/?format=json', 'https://archidekt.com/api/decks/123456/?format=json'],
    ['GET', '/card-images.archidekt.com/a/b.webp', 'https://card-images.archidekt.com/a/b.webp'],
    ['GET', '/card-images.archidekt.com/a/b.jpg', 'https://card-images.archidekt.com/a/b.jpg'],
    ['GET', '/card-images.archidekt.com/a/b.jpeg', 'https://card-images.archidekt.com/a/b.jpeg'],
    ['GET', '/card-images.archidekt.com/b.png', 'https://card-images.archidekt.com/b.png'],
    ['GET', '/storage.googleapis.com/archidekt-card-images/a/b.webp', 'https://storage.googleapis.com/archidekt-card-images/a/b.webp'],
    ['GET', '/storage.googleapis.com/topdekt-user/images/shadow.jpg', 'https://storage.googleapis.com/topdekt-user/images/shadow.jpg'],
    ['GET', '/cloud.manabox.app/decks/XYZxxa-YZ99Yyyx_yzXzzz', 'https://cloud.manabox.app/decks/XYZxxa-YZ99Yyyx_yzXzzz'],
    ['POST', spellbook, 'https://backend.commanderspellbook.com/estimate-bracket'],
  ]
  for (const [method, path, upstreamUrl] of rows) {
    const { response, calls } = await send(path, method === 'POST' ? json : undefined)
    assert.equal(response.status, 200, path)
    assert.equal(calls.length, 1, path)
    assert.equal(calls[0].url, upstreamUrl)
    assert.equal(calls[0].method, method, path)
  }
})

test('a GET forwards only Accept and the relay User-Agent', async () => {
  const headers = {
    origin,
    accept: 'application/json',
    cookie: 'session=1',
    authorization: 'Bearer secret',
    referer: `${origin}/`,
    'accept-language': 'en',
    'x-custom': '1',
  }
  const { calls } = await send('/archidekt.com/api/decks/v3/', { headers })
  assert.deepEqual(calls[0].headers, [
    ['accept', 'application/json'],
    ['user-agent', 'Cockatrice-web-relay'],
  ])
})

test('a GET without Accept forwards only the relay User-Agent', async () => {
  const { calls } = await send(image)
  assert.deepEqual(calls[0].headers, [['user-agent', 'Cockatrice-web-relay']])
})

test('a POST forwards only its body, Content-Type, Accept and the relay User-Agent', async () => {
  const headers = { ...json.headers, accept: '*/*', cookie: 'session=1', authorization: 'Bearer secret', 'x-custom': '1' }
  const { calls } = await send(spellbook, { ...json, headers })
  assert.deepEqual(calls[0].headers, [
    ['accept', '*/*'],
    ['content-type', 'application/json'],
    ['user-agent', 'Cockatrice-web-relay'],
  ])
  assert.equal(await new Response(calls[0].body).text(), json.body)
})

test('only Content-Type, Cache-Control, ETag and Last-Modified are copied from the upstream answer', async () => {
  const upstreamHeaders = {
    'content-type': 'image/png',
    'cache-control': 'max-age=60',
    etag: '"abc"',
    'last-modified': 'Wed, 21 Oct 2026 07:28:00 GMT',
    location: 'https://elsewhere.example/',
    server: 'upstream',
    'x-custom': '1',
  }
  const { response } = await send(image, undefined, () => new Response('png', { headers: upstreamHeaders }))
  assert.deepEqual([...response.headers], [
    ['access-control-allow-origin', origin],
    ['cache-control', 'max-age=60'],
    ['content-type', 'image/png'],
    ['etag', '"abc"'],
    ['last-modified', 'Wed, 21 Oct 2026 07:28:00 GMT'],
    ['vary', 'Origin'],
  ])
})

test('an upstream Set-Cookie does not reach the client', async () => {
  const { response } = await send(image, undefined, () => new Response('png', { headers: { 'set-cookie': 'tracker=1' } }))
  assert.equal(response.headers.get('set-cookie'), null)
})

test('upstream Access-Control headers do not reach the client', async () => {
  const upstreamHeaders = {
    'access-control-allow-origin': '*',
    'access-control-allow-credentials': 'true',
    'access-control-expose-headers': 'x-custom',
  }
  const { response } = await send(image, undefined, () => new Response('png', { headers: upstreamHeaders }))
  assert.equal(response.headers.get('access-control-allow-origin'), origin)
  assert.equal(response.headers.get('access-control-allow-credentials'), null)
  assert.equal(response.headers.get('access-control-expose-headers'), null)
})

test('the upstream status and body are returned unchanged', async () => {
  const { response } = await send(image, undefined, () => new Response('not here', { status: 404 }))
  assert.equal(response.status, 404)
  assert.equal(await response.text(), 'not here')
  assertCors(response)
})

test('the upstream body reaches the client before it ends', { timeout: 2000 }, async () => {
  const body = new ReadableStream({ start: controller => controller.enqueue(new TextEncoder().encode('first')) })
  const { response } = await send(image, undefined, () => new Response(body))
  const reader = response.body.getReader()
  assert.equal(new TextDecoder().decode((await reader.read()).value), 'first')
  await reader.cancel()
})

test('an upstream redirect is passed back, not followed', async () => {
  const redirect = () => new Response(null, { status: 302, headers: { location: 'https://elsewhere.example/' } })
  const { response, calls } = await send(image, undefined, redirect)
  assert.equal(calls[0].redirect, 'manual')
  assert.equal(response.status, 302)
  assert.equal(response.headers.get('location'), null)
})

test('a success carries the CORS headers', async () => {
  const { response } = await send(image)
  assert.equal(response.status, 200)
  assertCors(response)
})

test('a relay error carries the CORS headers', async () => {
  const { response } = await send('/example.com/a.png')
  assert.equal(response.status, 404)
  assertCors(response)
})

test('a preflight on a table route answers 204 with the route method', async () => {
  for (const [path, method] of [[image, 'GET'], [spellbook, 'POST']]) {
    const { response, calls } = await send(path, { method: 'OPTIONS' })
    assert.equal(response.status, 204, path)
    assert.equal(response.headers.get('access-control-allow-methods'), method)
    assert.equal(response.headers.get('access-control-allow-headers'), 'content-type')
    assert.equal(response.headers.get('access-control-max-age'), '86400')
    assertCors(response)
    assert.equal(calls.length, 0)
  }
})

test('a preflight off the table is not found', async () => {
  const { response, calls } = await send('/archidekt.com/api/users/', { method: 'OPTIONS' })
  assert.equal(response.status, 404)
  assertCors(response)
  assert.equal(calls.length, 0)
})

test('a request without Origin is refused before the table is read', async () => {
  for (const path of [image, '/example.com/a.png']) {
    for (const method of ['GET', 'OPTIONS']) {
      const { response, calls } = await send(path, { method, headers: {} })
      assert.equal(response.status, 403, `${method} ${path}`)
      assert.equal(response.headers.get('access-control-allow-origin'), null)
      assert.equal(calls.length, 0)
    }
  }
})

test('a request from an origin not in the list is refused', async () => {
  const { response, calls } = await send(image, { headers: { origin: 'https://other.example' } })
  assert.equal(response.status, 403)
  assert.equal(response.headers.get('access-control-allow-origin'), null)
  assert.equal(calls.length, 0)
})

test('spaces around the listed origins are ignored', async () => {
  const list = { ALLOWED_ORIGINS: ' https://other.example ,  https://example.github.io ' }
  const { response } = await send(image, undefined, undefined, list)
  assert.equal(response.status, 200)
  assertCors(response)
})

test('an empty or missing origin list admits nobody', async () => {
  for (const environment of [{ ALLOWED_ORIGINS: '' }, {}]) {
    for (const headers of [{ origin }, { origin: '' }]) {
      const { response, calls } = await send(image, { headers }, undefined, environment)
      assert.equal(response.status, 403, JSON.stringify([environment, headers]))
      assert.equal(calls.length, 0)
    }
  }
})

test('an unknown host is not found', async () => {
  const { response, calls } = await send('/example.com/a.png')
  assert.equal(response.status, 404)
  assert.equal(calls.length, 0)
})

test('a host with a port, user info or upper case is not found', async () => {
  for (const host of ['archidekt.com:443', 'user@archidekt.com', 'Archidekt.com']) {
    const { response, calls } = await send(`/${host}/api/decks/v3/`)
    assert.equal(response.status, 404, host)
    assert.equal(calls.length, 0)
  }
})

test('a path off the table is not found', async () => {
  const paths = [
    '/archidekt.com/api/decks/v3',
    '/archidekt.com/api/decks/v3/x/',
    '/archidekt.com/api/decks/12a/',
    '/archidekt.com/api/decks/123',
    '/archidekt.com/api/users/',
    '/card-images.archidekt.com/a.gif',
    '/card-images.archidekt.com/a.png.html',
    '/storage.googleapis.com/other-bucket/a.png',
    '/storage.googleapis.com/archidekt-card-images-copy/a.png',
    '/storage.googleapis.com/topdekt-user/a.png',
    '/storage.googleapis.com/archidekt-card-images/a.txt',
    '/cloud.manabox.app/decks/',
    '/cloud.manabox.app/decks/a.b',
    '/cloud.manabox.app/decks/a/b',
    `${spellbook}/`,
  ]
  for (const path of paths) {
    const { response, calls } = await send(path)
    assert.equal(response.status, 404, path)
    assert.equal(calls.length, 0)
  }
})

for (const [form, path] of [
  ['..', '/card-images.archidekt.com/a..b.png'],
  ['//', '/card-images.archidekt.com//a.png'],
  ['%2f', '/card-images.archidekt.com/a%2fb.png'],
  ['%2F', '/card-images.archidekt.com/a%2Fb.png'],
  ['%5c', '/card-images.archidekt.com/a%5cb.png'],
  ['%5C', '/card-images.archidekt.com/a%5Cb.png'],
  ['%2e', '/card-images.archidekt.com/a%2e/b.png'],
  ['%2E', '/card-images.archidekt.com/a%2E/b.png'],
]) {
  test(`a path containing ${form} is refused`, async () => {
    const { response, calls } = await send(path)
    assert.equal(response.status, 404)
    assert.equal(calls.length, 0)
  })
}

test('a backslash in the raw request path is refused', async () => {
  const raw = { method: 'GET', url: `${relay}/card-images.archidekt.com/a\\b.png`, headers: new Headers({ origin }) }
  let called = false
  const response = await handle(raw, env, async () => ((called = true), new Response('png')))
  assert.equal(response.status, 404)
  assert.equal(called, false)
})

test('a wrong query on a query-restricted row is not found', async () => {
  const requests = [
    ['/archidekt.com/api/decks/123/?format=xml'],
    ['/archidekt.com/api/decks/123/?format=json&page=2'],
    ['/archidekt.com/api/decks/123/?page=2'],
    [`${image}?v=1`],
    ['/storage.googleapis.com/archidekt-card-images/a.png?v=1'],
    ['/cloud.manabox.app/decks/abc?v=1'],
    [`${spellbook}?v=1`, json],
  ]
  for (const [path, init] of requests) {
    const { response, calls } = await send(path, init)
    assert.equal(response.status, 404, path)
    assert.equal(calls.length, 0)
  }
})

test('a wrong method on a table route answers 405 with Allow', async () => {
  for (const [path, init, allow] of [
    ['/archidekt.com/api/decks/v3/', json, 'GET'],
    [image, { method: 'HEAD' }, 'GET'],
    [spellbook, undefined, 'POST'],
  ]) {
    const { response, calls } = await send(path, init)
    assert.equal(response.status, 405, path)
    assert.equal(response.headers.get('allow'), allow)
    assertCors(response)
    assert.equal(calls.length, 0)
  }
})

test('a POST with a content type other than JSON answers 415', async () => {
  for (const type of [null, 'text/plain', 'application/x-www-form-urlencoded', 'application/jsonp', 'application/json-patch+json']) {
    const headers = type === null ? { origin } : { origin, 'content-type': type }
    const { response, calls } = await send(spellbook, { ...json, headers })
    assert.equal(response.status, 415, type)
    assertCors(response)
    assert.equal(calls.length, 0)
  }
})

test('a POST with JSON parameters or another case is accepted', async () => {
  for (const type of ['application/json; charset=utf-8', 'Application/JSON']) {
    const { response, calls } = await send(spellbook, { ...json, headers: { origin, 'content-type': type } })
    assert.equal(response.status, 200, type)
    assert.deepEqual(calls[0].headers.find(([name]) => name === 'content-type'), ['content-type', type])
  }
})

test('a POST body over the limit answers 413 even with a false Content-Length', async () => {
  const headers = { ...json.headers, 'content-length': '2' }
  const { response, calls } = await send(spellbook, { ...json, headers, body: streamOf(262145) })
  assert.equal(response.status, 413)
  assertCors(response)
  assert.equal(calls.length, 0)
})

test('a POST body of exactly the limit is forwarded', async () => {
  const { response, calls } = await send(spellbook, { ...json, body: streamOf(262144) })
  assert.equal(response.status, 200)
  assert.equal((await new Response(calls[0].body).arrayBuffer()).byteLength, 262144)
})

test('an upstream fetch that throws answers 502', async () => {
  const { response } = await send(image, undefined, () => {
    throw new TypeError('fetch failed')
  })
  assert.equal(response.status, 502)
  assertCors(response)
})
