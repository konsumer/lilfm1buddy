// lilfm1buddy's page: tabs, the install button's firmware, and the board's USB drive (BOOT on a computer), opened as a
// folder (File System Access: Chrome, Edge). Its files: *.fwsc firmwares, *.bin backups, lilfm1.bin and lilsmk.bin (the
// menus), midimap.txt, log.txt, chip.txt, map.txt (esp32/main.cpp)

const LOADER = 'wl82loader.bin' // (JieLi's: the board doesn't need it any more, and it's kept out of the list)
const MENUS = { 'lilfm1.bin': 'FM-1', 'lilsmk.bin': 'SMK-37' } // the menus on their screens (picker/): the board needs the one for each device it's used on
const CLOUD = 'https://mutant.cloud/' // every FM-1 and SMK-37 firmware: firmware.json, and its packages (dl/) with CORS on

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
  const log = await read('log.txt')
  const logText = log ? await log.text() : ''
  const files = []
  for await (const [name, h] of drive.entries()) {
    if (h.kind === 'file' && isFirmware(name)) files.push(await h.getFile())
  }
  files.sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }))
  // which device the board is used with: what its log last saw ("It's an FM-1 (...)"), or whose backups are here. Only
  // that one's menu is needed, so only it gets warned about (an FM-1's owner isn't told about lilsmk.bin, or the other way)
  const kind = /It's an SMK-37/.test(logText) || files.some((f) => /^smk-\d+\.bin$/i.test(f.name)) ? 'SMK-37' : /It's an FM-1/.test(logText) || files.some((f) => /^fm1-\d+\.bin$/i.test(f.name)) ? 'FM-1' : ''
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
    if (menu) p.firstChild.textContent = `${device}: ${name} on the drive${same ? ', the latest.' : '.'}`
    else if (device === kind) {
      p.firstChild.innerHTML = '<span class="bad"></span>'
      p.firstChild.firstChild.textContent = `${device}: no ${name} on the drive, and the board can't show its menu without it.`
    } else p.firstChild.textContent = `${device}: no ${name} on the drive (only needed if you have one).`
    p.lastChild.hidden = !latest || same
    p.lastChild.addEventListener('click', () => run(`Getting the latest ${device} menu…`, async () => put(name, new Uint8Array(latest))))
    $('#menus').append(p)
  }
  $('#map').textContent = (await read('midimap.txt')) ? 'midimap.txt is on the drive: the board routes MIDI on the FM-1.' : 'No midimap.txt: no MIDI routing.'
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
  $('#log').textContent = logText || '(nothing yet)'
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

$('#add').addEventListener('change', async (ev) => {
  const files = [...ev.target.files]
  ev.target.value = ''
  run('Adding…', async () => {
    for (const f of files) await put(f.name.replace(/[^A-Za-z0-9._+-]/g, '_'), f)
  })
})

// ---- mutant.cloud's firmwares: one choice for each FM-1 or SMK-37 package in each one's latest release (grouped by
// device), downloaded through mutant.cloud/dl/ID/FILE (GitHub's downloads can't be read from a page) and checked against
// its size and SHA-256 ----
let hub = []

// who a package is for, from its product ("FM-1_015", "SMK-37 Elite_016"): the SMK-37's names are all one keyboard
const DEVICES = [
  ['FM-1', /^FM-1_/],
  ['SMK-37', /^(SMK-37 (Pro|Elite)|MKE-P37)_/]
]
const deviceOf = (product) => (DEVICES.find(([, re]) => re.test(product || '')) || [])[0]

const label = (f) => `${f.name} ${f.version}${f.variant ? ` (${f.variant})` : ''}${f.beta ? ' beta' : ''}, ${f.author}`

fetch(CLOUD + 'firmware.json')
  .then((r) => (r.ok ? r.json() : Promise.reject(new Error(r.status))))
  .then((c) => {
    for (const e of c.entries || []) {
      if (e.kind !== 'firmware' || e.archived || !e.release) continue
      const pkgs = (e.release.assets || []).filter((a) => deviceOf(a.product) && a.size && /^[0-9a-f]{64}$/.test(a.sha256 || '') && !a.missing)
      for (const a of pkgs) {
        const device = deviceOf(a.product)
        const variant = pkgs.filter((b) => deviceOf(b.product) === device).length > 1 ? a.name.replace(/\.fwsc$/i, '') : ''
        hub.push({ id: e.id, device, file: a.name, size: a.size, sha256: a.sha256, name: e.name, author: e.author, beta: e.beta, what: e.description || '', variant, version: e.id === 'stock' ? a.product.replace(/^.*_0*/, 'V') : e.release.version.replace(/^v/, '') })
      }
    }
    hub.sort((a, b) => (b.id === 'stock') - (a.id === 'stock') || a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }) || a.file.localeCompare(b.file)) // (M-VAVE's own first)
    const sel = $('#hub')
    sel.innerHTML = '<option value="">Pick a firmware…</option>'
    for (const [device] of DEVICES) {
      const g = document.createElement('optgroup')
      g.label = device
      hub.forEach((f, i) => {
        if (f.device !== device) return
        const o = document.createElement('option')
        o.value = i
        o.textContent = `${label(f)}, ${kb(f.size)}`
        g.append(o)
      })
      if (g.children.length) sel.append(g)
    }
    sel.disabled = false
  })
  .catch(() => {
    $('#hub').innerHTML = "<option>mutant.cloud's list didn't come</option>"
  })

$('#hub').addEventListener('change', () => {
  const f = hub[$('#hub').value]
  $('#hubwhat').textContent = f ? f.what : ''
  $('#addhub').disabled = !f
})

$('#addhub').addEventListener('click', () => {
  const f = hub[$('#hub').value]
  if (!f) return
  run(`Getting ${label(f)}…`, async () => {
    const r = await fetch(`${CLOUD}dl/${f.id}/${encodeURIComponent(f.file)}`)
    if (!r.ok) throw new Error(`mutant.cloud didn't give it (${r.status}).`)
    const bytes = new Uint8Array(await r.arrayBuffer())
    if (bytes.length !== f.size || (await sha256(bytes)) !== f.sha256) throw new Error("It didn't come as mutant.cloud's list says: try again.")
    await put(f.file.replace(/[^A-Za-z0-9._+-]/g, '_'), bytes)
  })
})
