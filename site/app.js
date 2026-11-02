// lilfm1buddy's page: tabs, the install button's firmware, and the board's USB drive (BOOT on a computer), opened as a
// folder (File System Access: Chrome, Edge). Its files: *.fwsc firmwares, *.bin backups, wl82loader.bin, midimap.txt,
// log.txt (esp32/main.cpp)

const LOADER = 'wl82loader.bin'
const MENUS = { 'lilfm1.bin': 'FM-1', 'lilsmk.bin': 'SMK-37' } // the menus on their screens (picker/): the board needs the one for each device it's used on
const LOADER_URL = 'https://raw.githubusercontent.com/kagaimiq/jl-uboot-tool/c32b28e7a04f873f526ba929cb1bae9f05442fe9/data/loaderblobs/usb/wl82loader.bin'
const LOADER_SHA256 = 'd41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376'
const HUB = 'https://thefirmwarehub.com/' // the FM-1 Firmware Hub: catalog.json, and the packages next to it

const $ = (s) => document.querySelector(s)
const TRASH = '<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M3 6h18M8 6V4h8v2M19 6l-1 14H6L5 6M10 11v6M14 11v6"/></svg>'

// ---- tabs ----
function tab(name) {
  for (const a of document.querySelectorAll('.tabs a')) a.classList.toggle('on', a.dataset.tab === name)
  for (const p of document.querySelectorAll('[data-page]')) p.hidden = p.dataset.page !== name
}
for (const a of document.querySelectorAll('[data-tab]')) {
  a.addEventListener('click', () => tab(a.dataset.tab))
}
tab(location.hash.slice(1) || 'install')

// ---- the program next to the page, for the board picked (remembered). None until one is: another board's program
// doesn't start (another flash size, PSRAM), so nothing is installed until it's said which ----
function board(name) {
  $('#boardtype').value = name
  $('#install').hidden = true
  $('#nofw').hidden = true
  try {
    localStorage.board = name
  } catch (e) {}
  if (!name) return
  $('#install').setAttribute('manifest', `fw/${name}/manifest.json`)
  fetch(`fw/${name}/manifest.json`)
    .then((r) => (r.ok ? r.json() : Promise.reject(new Error('none'))))
    .then((m) => {
      $('#release').textContent = `Firmware ${m.version}`
      $('#install').hidden = false
      $('#nofw').hidden = true
    })
    .catch(() => {
      $('#install').hidden = true
      $('#nofw').hidden = false
    })
}
$('#boardtype').addEventListener('change', () => board($('#boardtype').value))
let saved = ''
try {
  saved = localStorage.board || ''
} catch (e) {}
board(saved)

// ---- the drive ----
let drive = null

const kb = (n) => `${Math.round(n / 1024).toLocaleString()} KB`
const isFirmware = (n) => n !== LOADER && !n.startsWith('.') && /\.(fwsc|bin)$/i.test(n) && !(n in MENUS)

function busy(text) {
  $('#busy').textContent = text || ''
  for (const b of document.querySelectorAll('#manage button, #add')) b.disabled = !!text
  if (!text) $('#addhub').disabled = !hub[$('#hub').value]
}

async function sha256(bytes) {
  const h = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))
  return [...h].map((b) => b.toString(16).padStart(2, '0')).join('')
}

async function read(name) {
  try {
    return await (await drive.getFileHandle(name)).getFile()
  } catch (e) {
    return null
  }
}

async function put(name, bytes) {
  const w = await (await drive.getFileHandle(name, { create: true })).createWritable()
  await w.write(bytes)
  await w.close()
}

async function refresh() {
  const loader = await read(LOADER)
  const ok = loader && (await sha256(await loader.arrayBuffer())) === LOADER_SHA256
  $('#loader').innerHTML = ok ? '<span class="good">On the drive.</span>' : '<span class="bad">Not on the drive yet: it can\'t write the FM-1 without it.</span>'
  $('#getloader').hidden = ok
  $('#menus').innerHTML = ''
  for (const [name, device] of Object.entries(MENUS)) {
    const menu = await read(name)
    const latest = await fetch('fw/' + name)
      .then((r) => (r.ok ? r.arrayBuffer() : null))
      .catch(() => null)
    const same = menu && latest && (await sha256(await menu.arrayBuffer())) === (await sha256(latest))
    const p = document.createElement('p')
    p.className = 'row'
    p.innerHTML = `<span></span><button class="button small">${menu ? 'Update it' : 'Put it on'}</button>`
    p.firstChild.textContent = `${device}: ${menu ? `${name} on the drive${same ? ', the latest.' : '.'}` : `no ${name} on the drive (only needed if you have one).`}`
    p.lastChild.hidden = !latest || same
    p.lastChild.addEventListener('click', () => run(`Getting the latest ${device} menu…`, async () => put(name, new Uint8Array(latest))))
    $('#menus').append(p)
  }
  $('#map').textContent = (await read('midimap.txt')) ? 'midimap.txt is on the drive: the board routes MIDI on the FM-1.' : 'No midimap.txt: no MIDI routing.'
  const files = []
  for await (const [name, h] of drive.entries()) {
    if (h.kind === 'file' && isFirmware(name)) files.push(await h.getFile())
  }
  files.sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }))
  const body = $('#files tbody')
  body.innerHTML = ''
  for (const f of files) {
    const tr = document.createElement('tr')
    tr.innerHTML = `<td></td><td class="num">${kb(f.size)}</td><td><button class="icon" title="Remove" aria-label="Remove ${f.name}">${TRASH}</button></td>`
    tr.firstChild.textContent = f.name
    tr.querySelector('button').addEventListener('click', () => run(`Removing ${f.name}…`, () => drive.removeEntry(f.name)))
    body.append(tr)
  }
  if (!files.length) body.innerHTML = '<tr><td colspan="3" class="dim">None yet.</td></tr>'
  const log = await read('log.txt')
  $('#log').textContent = log ? await log.text() : '(nothing yet)'
}

// one thing at a time, then the list again
async function run(what, fn) {
  busy(what)
  try {
    await fn()
    busy()
    await refresh()
  } catch (e) {
    busy()
    $('#busy').innerHTML = `<span class="bad"></span>`
    $('#busy span').textContent = e.message
  }
}

if (!('showDirectoryPicker' in window)) {
  $('#open').disabled = true
  $('#nofs').hidden = false
}

$('#open').addEventListener('click', async () => {
  try {
    drive = await window.showDirectoryPicker({ id: 'lilfm1', mode: 'readwrite' })
    $('#drive').textContent = drive.name
    $('#manage').hidden = false
    await run('Reading it…', async () => {})
  } catch (e) {
    if (e.name !== 'AbortError') {
      $('#drive').innerHTML = '<span class="bad"></span>'
      $('#drive span').textContent = e.message
    }
  }
})

$('#getloader').addEventListener('click', () =>
  run("Getting JieLi's loader…", async () => {
    const r = await fetch(LOADER_URL)
    if (!r.ok) throw new Error(`Couldn't get it (${r.status}).`)
    const bytes = new Uint8Array(await r.arrayBuffer())
    if ((await sha256(bytes)) !== LOADER_SHA256) throw new Error("That isn't the loader it should be.")
    await put(LOADER, bytes)
  })
)

$('#add').addEventListener('change', async (ev) => {
  const files = [...ev.target.files]
  ev.target.value = ''
  run('Adding…', async () => {
    for (const f of files) await put(f.name.replace(/[^A-Za-z0-9._+-]/g, '_'), f)
  })
})

// ---- the Hub's firmwares ----
let hub = []

const label = (f) => `${f.name}${f.version ? ' ' + f.version : ''}${f.author ? ' (' + f.author + ')' : ''}${f.beta ? ' beta' : ''}`

fetch(HUB + 'catalog.json')
  .then((r) => (r.ok ? r.json() : Promise.reject(new Error(r.status))))
  .then((c) => {
    const ok = (f) => f.pkg && f.size && /^[0-9a-f]{64}$/.test(f.sha256 || '') && !f.pkg.includes('..')
    hub = (c.firmwares || []).filter(ok).sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }))
    if (c.stock && ok(c.stock)) hub.unshift({ ...c.stock, name: `M-VAVE stock ${c.stock.product.replace('FM-1_0', 'V')}`, what: "The FM-1's own firmware." })
    const sel = $('#hub')
    sel.innerHTML = '<option value="">Pick a firmware…</option>'
    hub.forEach((f, i) => {
      const o = document.createElement('option')
      o.value = i
      o.textContent = `${label(f)}, ${kb(f.size)}`
      sel.append(o)
    })
    sel.disabled = false
  })
  .catch(() => {
    $('#hub').innerHTML = "<option>The Hub's list didn't come</option>"
  })

$('#hub').addEventListener('change', () => {
  const f = hub[$('#hub').value]
  $('#hubwhat').textContent = f ? f.what || '' : ''
  $('#addhub').disabled = !f
})

$('#addhub').addEventListener('click', () => {
  const f = hub[$('#hub').value]
  if (!f) return
  const name = f.pkg
    .split('/')
    .pop()
    .replace(/[^A-Za-z0-9._+-]/g, '_')
  run(`Getting ${label(f)}…`, async () => {
    const r = await fetch(HUB + f.pkg)
    if (!r.ok) throw new Error(`The Hub didn't give it (${r.status}).`)
    const bytes = new Uint8Array(await r.arrayBuffer())
    if (bytes.length !== f.size || (await sha256(bytes)) !== f.sha256) throw new Error("It didn't come as the Hub's list says: try again.")
    await put(name, bytes)
  })
})
