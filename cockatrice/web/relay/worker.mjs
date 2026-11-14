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

const routes = [
  ['archidekt.com', 'GET', /^\/api\/decks\/v3\/$/, /^/],
  ['archidekt.com', 'GET', /^\/api\/decks\/\d+\/$/, /^(format=json)?$/],
  ['card-images.archidekt.com', 'GET', /^\/.*\.(webp|jpe?g|png)$/, /^$/],
  ['storage.googleapis.com', 'GET', /^\/(archidekt-card-images|topdekt-user\/images)\/.*\.(webp|jpe?g|png)$/, /^$/],
  ['cloud.manabox.app', 'GET', /^\/decks\/[\w-]+$/, /^$/],
  ['backend.commanderspellbook.com', 'POST', /^\/estimate-bracket$/, /^$/],
]
const tricks = /\.\.|\/\/|%2f|%5c|%2e/i
const bodyLimit = 262144
const copiedHeaders = ['content-type', 'cache-control', 'etag', 'last-modified']

async function readBody(stream) {
  const chunks = []
  let size = 0
  for await (const chunk of stream ?? []) {
    size += chunk.byteLength
    if (size > bodyLimit) return null
    chunks.push(chunk)
  }
  return new Blob(chunks)
}

export async function handle(request, env, upstreamFetch = fetch) {
  const origin = request.headers.get('origin')
  const origins = (env.ALLOWED_ORIGINS ?? '').split(',').map(entry => entry.trim())
  if (!origin || !origins.includes(origin)) return new Response('Forbidden', { status: 403 })
  const cors = { 'access-control-allow-origin': origin, vary: 'Origin' }
  const answer = (status, text, headers) => new Response(text, { status, headers: { ...cors, ...headers } })

  const url = new URL(request.url)
  const [, host, path] = url.pathname.match(/^\/([^/]*)(.*)$/)
  const route = routes.find(([h, , p, q]) => h === host && p.test(path) && q.test(url.search.slice(1)))
  // The URL parser turns a backslash into a slash, so only the raw URL still shows one.
  if (!route || tricks.test(url.pathname) || request.url.split('?')[0].includes('\\')) return answer(404, 'Not found')
  const method = route[1]
  if (request.method === 'OPTIONS')
    return answer(204, null, {
      'access-control-allow-methods': method,
      'access-control-allow-headers': 'content-type',
      'access-control-max-age': '86400',
    })
  if (request.method !== method) return answer(405, 'Method not allowed', { allow: method })

  const headers = { 'user-agent': 'Cockatrice-web-relay' }
  if (request.headers.has('accept')) headers.accept = request.headers.get('accept')
  let body
  if (method === 'POST') {
    headers['content-type'] = request.headers.get('content-type')
    if (headers['content-type']?.split(';')[0].trim().toLowerCase() !== 'application/json')
      return answer(415, 'Unsupported media type')
    body = await readBody(request.body)
    if (!body) return answer(413, 'Payload too large')
  }

  let upstream
  try {
    // Following a redirect would send a request the table did not admit.
    upstream = await upstreamFetch(`https://${host}${path}${url.search}`, { method, headers, body, redirect: 'manual' })
  } catch {
    return answer(502, 'Bad gateway')
  }
  const returned = new Headers(cors)
  for (const name of copiedHeaders) if (upstream.headers.has(name)) returned.set(name, upstream.headers.get(name))
  return new Response(upstream.body, { status: upstream.status, headers: returned })
}

export default { fetch: (request, env) => handle(request, env) }
