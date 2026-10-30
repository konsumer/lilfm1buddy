// lilfm1buddy's page: tabs, the install button's firmware, and the board's files over Web Serial (esp32/main.cpp's
// commands: info, ls, rm NAME, put NAME BYTES, log)

const LOADER_URL = 'https://raw.githubusercontent.com/kagaimiq/jl-uboot-tool/c32b28e7a04f873f526ba929cb1bae9f05442fe9/data/loaderblobs/usb/wl82loader.bin'
const LOADER_SHA256 = 'd41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376'
const PIECE = 4096 // what the board takes before an ACK

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

// ---- the firmware next to the page ----
fetch('fw/manifest.json')
  .then((r) => (r.ok ? r.json() : Promise.reject(new Error('none'))))
  .then((m) => {
    $('#release').textContent = `Firmware ${m.version}`
  })
  .catch(() => {
    $('#install').hidden = true
    $('#nofw').hidden = false
  })

// ---- the board, over serial ----
class Board {
  async open() {
    this.port = await navigator.serial.requestPort()
    await this.port.open({ baudRate: 115200 })
    this.lines = []
    this.waiting = null
    this.reading = this.read()
  }

  async read() {
    const decoder = new TextDecoder()
    let rest = ''
    this.reader = this.port.readable.getReader()
    try {
      for (;;) {
        const { value, done } = await this.reader.read()
        if (done) break
        rest += decoder.decode(value, { stream: true })
        let i
        while ((i = rest.indexOf('\n')) >= 0) {
          this.lines.push(rest.slice(0, i).replace(/\r$/, ''))
          rest = rest.slice(i + 1)
        }
        if (this.waiting) this.waiting()
      }
    } catch (e) {}
  }

  async close() {
    try {
      await this.reader.cancel()
      await this.reading
      await this.port.close()
    } catch (e) {}
  }

  async send(text) {
    await this.write(new TextEncoder().encode(text + '\n'))
  }

  async write(bytes) {
    const w = this.port.writable.getWriter()
    await w.write(bytes)
    w.releaseLock()
  }

  // the next line that test() likes (others are the board's log), within ms
  async line(test, ms = 5000) {
    const end = Date.now() + ms
    for (;;) {
      const i = this.lines.findIndex(test)
      if (i >= 0) return this.lines.splice(0, i + 1).pop()
      const left = end - Date.now()
      if (left <= 0) throw new Error('The board stopped answering.')
      await new Promise((resolve) => {
        this.waiting = resolve
        setTimeout(resolve, left)
      })
      this.waiting = null
    }
  }

  async command(text, test, ms) {
    this.lines = []
    await this.send(text)
    return this.line(test, ms)
  }

  async info() {
    const l = await this.command('info', (l) => l.startsWith('INFO '))
    const [, name, version, ...kv] = l.split(' ')
    const info = { name, version }
    for (const p of kv) {
      const [k, v] = p.split('=')
      info[k] = Number(v)
    }
    return info
  }

  async ls() {
    this.lines = []
    await this.send('ls')
    const files = []
    for (;;) {
      const l = await this.line((l) => l.startsWith('FILE ') || l === 'END')
      if (l === 'END') return files
      const [, name, size] = l.split(' ')
      files.push({ name, size: Number(size) })
    }
  }

  async rm(name) {
    const l = await this.command(`rm ${name}`, (l) => l === 'OK' || l.startsWith('ERR '))
    if (l !== 'OK') throw new Error(l.slice(4))
  }

  async put(name, bytes, progress) {
    let l = await this.command(`put ${name} ${bytes.length}`, (l) => l === 'READY' || l.startsWith('ERR '))
    if (l !== 'READY') throw new Error(l.slice(4))
    for (let at = 0; at < bytes.length; at += PIECE) {
      await this.write(bytes.subarray(at, at + PIECE))
      l = await this.line((l) => l.startsWith('ACK ') || l.startsWith('ERR '), 10000)
      if (l.startsWith('ERR ')) throw new Error(l.slice(4))
      progress?.(Math.min(at + PIECE, bytes.length), bytes.length)
    }
    l = await this.line((l) => l.startsWith('OK ') || l.startsWith('ERR '), 30000)
    if (l.startsWith('ERR ')) throw new Error(l.slice(4))
  }

  // everything up to END (the log is the board's own lines: all of them)
  async log() {
    this.lines = []
    await this.send('log')
    const end = Date.now() + 10000
    while (!this.lines.includes('END')) {
      if (Date.now() > end) throw new Error('The board stopped answering.')
      await new Promise((resolve) => setTimeout(resolve, 100))
    }
    const out = this.lines.slice(0, this.lines.indexOf('END'))
    this.lines = []
    return out.join('\n')
  }
}

// ---- the Firmwares tab ----
let board = null

const kb = (n) => `${Math.round(n / 1024).toLocaleString()} KB`

function busy(text) {
  $('#busy').textContent = text || ''
  for (const b of document.querySelectorAll('#manage button, #add')) b.disabled = !!text
}

async function sha256(bytes) {
  const h = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))
  return [...h].map((b) => b.toString(16).padStart(2, '0')).join('')
}

async function refresh() {
  const info = await board.info()
  $('#board').textContent = `${info.name} ${info.version}`
  $('#loader').innerHTML = info.loader ? '<span class="good">On the board.</span>' : '<span class="bad">Not on the board yet: it can\'t write the FM-1 without it.</span>'
  $('#getloader').hidden = !!info.loader
  $('#space').textContent = `${kb(info.free)} free of ${kb(info.total)}. The FM-1's menu shows the first 24, by name.`
  const files = (await board.ls()).sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }))
  const body = $('#files tbody')
  body.innerHTML = ''
  for (const f of files) {
    const tr = document.createElement('tr')
    tr.innerHTML = `<td></td><td class="num">${kb(f.size)}</td><td><button class="icon" title="Remove" aria-label="Remove ${f.name}">${TRASH}</button></td>`
    tr.firstChild.textContent = f.name.replace(/\.fwsc$/i, '')
    tr.querySelector('button').addEventListener('click', () => run(`Removing ${f.name}…`, () => board.rm(f.name)))
    body.append(tr)
  }
  if (!files.length) body.innerHTML = '<tr><td colspan="3" class="dim">None yet.</td></tr>'
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

if (!('serial' in navigator)) {
  $('#connect').disabled = true
  $('#noserial').hidden = false
}

$('#connect').addEventListener('click', async () => {
  if (board) {
    await board.close()
    board = null
    $('#manage').hidden = true
    $('#connect').textContent = 'Connect'
    $('#board').textContent = ''
    return
  }
  try {
    board = new Board()
    await board.open()
    $('#board').textContent = 'Asking it…'
    let ok = false
    for (let t = 0; t < 4 && !ok; t++) {
      // (opening the port may restart it: it listens again a moment later)
      try {
        await refresh()
        ok = true
      } catch (e) {}
    }
    if (!ok) throw new Error('No answer: is lilfm1buddy installed on it? (Install tab)')
    $('#manage').hidden = false
    $('#connect').textContent = 'Disconnect'
  } catch (e) {
    if (board) await board.close()
    board = null
    $('#board').innerHTML = '<span class="bad"></span>'
    $('#board span').textContent = e.message
  }
})

$('#getloader').addEventListener('click', () =>
  run("Getting JieLi's loader…", async () => {
    const r = await fetch(LOADER_URL)
    if (!r.ok) throw new Error(`Couldn't get it (${r.status}).`)
    const bytes = new Uint8Array(await r.arrayBuffer())
    if ((await sha256(bytes)) !== LOADER_SHA256) throw new Error("That isn't the loader it should be.")
    await board.put('wl82loader.bin', bytes, (done, total) => busy(`Sending JieLi's loader… ${Math.round((done * 100) / total)}%`))
  })
)

$('#add').addEventListener('change', async (ev) => {
  const files = [...ev.target.files]
  ev.target.value = ''
  run('Adding…', async () => {
    for (const f of files) {
      const name = f.name.replace(/[^A-Za-z0-9._+-]/g, '_')
      const bytes = new Uint8Array(await f.arrayBuffer())
      await board.put(name, bytes, (done, total) => busy(`${name}: ${Math.round((done * 100) / total)}%`))
    }
  })
})

$('#getlog').addEventListener('click', () =>
  run('Reading its log…', async () => {
    $('#log').textContent = (await board.log()) || '(nothing yet)'
  })
)
