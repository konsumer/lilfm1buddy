// Try it: the menu (picker/) in the browser, from fw/menu.wasm (tools/emu/emu.c): the same screens and workings, with
// a pretend board. Its keys are the FM-1's (their ids: fomajam's fm1.h), and the ones the menu takes glow

const $ = (s) => document.querySelector(s)
const canvas = $('#emu-screen')
const KEYS = { Enter: 1, Backspace: 0, h: 8, s: 9, g: 7, e: 6, r: 13 } // (key: button id)

let exports,
  img,
  steps = 0,
  btn = 0,
  notes = 0,
  angle = 0,
  started = false

async function start() {
  if (started) return
  started = true
  const { instance } = await WebAssembly.instantiateStreaming(fetch('fw/menu.wasm'), {}).catch(async () => {
    const r = await fetch('fw/menu.wasm')
    return WebAssembly.instantiate(await r.arrayBuffer(), {})
  })
  exports = instance.exports
  exports.emu_init()
  img = canvas.getContext('2d').createImageData(240, 240)
  requestAnimationFrame(frame)
}

function turn(n) {
  steps += n
  angle += n * 24
  $('#emu-sel span').style.transform = `rotate(${angle}deg)`
}

function frame(t) {
  requestAnimationFrame(frame)
  if (canvas.closest('[data-page]').hidden) return
  const lit = exports.emu_frame(t >>> 0, steps, btn, notes, 0)
  steps = btn = notes = 0
  for (const b of document.querySelectorAll('[data-btn]')) b.classList.toggle('lit', !!(lit & (1 << b.dataset.btn)))
  const fb = new Uint16Array(exports.memory.buffer, exports.emu_fb(), 240 * 240)
  const d = img.data
  for (let i = 0, j = 0; i < fb.length; i++, j += 4) {
    const c = fb[i]
    d[j] = ((c >> 11) * 527 + 23) >> 6
    d[j + 1] = (((c >> 5) & 63) * 259 + 33) >> 6
    d[j + 2] = ((c & 31) * 527 + 23) >> 6
    d[j + 3] = 255
  }
  canvas.getContext('2d').putImageData(img, 0, 0)
}

for (const b of document.querySelectorAll('[data-btn]')) b.addEventListener('click', () => (btn |= 1 << b.dataset.btn))
for (const b of document.querySelectorAll('[data-step]')) b.addEventListener('click', () => turn(+b.dataset.step))
for (const k of document.querySelectorAll('[data-note]')) k.addEventListener('click', () => (notes |= 1 << k.dataset.note))
$('#emu-boot').addEventListener('click', () => exports && exports.emu_boot())

// the knob: scrolled, or dragged up and down
const knob = $('#emu-sel')
let wheel = 0,
  dragY = null
canvas.addEventListener('wheel', scroll, { passive: false })
knob.addEventListener('wheel', scroll, { passive: false })
function scroll(e) {
  e.preventDefault()
  wheel += e.deltaY
  while (Math.abs(wheel) >= 40) {
    turn(Math.sign(wheel))
    wheel -= Math.sign(wheel) * 40
  }
}
knob.addEventListener('pointerdown', (e) => {
  dragY = e.clientY
  knob.setPointerCapture(e.pointerId)
})
knob.addEventListener('pointermove', (e) => {
  if (dragY === null) return
  while (Math.abs(e.clientY - dragY) >= 16) {
    const s = Math.sign(e.clientY - dragY)
    turn(s)
    dragY += s * 16
  }
})
knob.addEventListener('pointerup', () => (dragY = null))

document.addEventListener('keydown', (e) => {
  if (canvas.closest('[data-page]').hidden || e.metaKey || e.ctrlKey || e.altKey || e.target.closest('input, select, textarea')) return
  const k = e.key.length === 1 ? e.key.toLowerCase() : e.key
  if (k === 'ArrowDown' || k === 'ArrowRight') turn(1)
  else if (k === 'ArrowUp' || k === 'ArrowLeft') turn(-1)
  else if (k in KEYS) btn |= 1 << KEYS[k]
  else if (k >= '1' && k <= '8') notes |= 1 << (k - 1)
  else if (k === 'b' && exports) exports.emu_boot()
  else return
  e.preventDefault()
})

// once it's on screen (the tab)
new MutationObserver(() => !canvas.closest('[data-page]').hidden && start()).observe(canvas.closest('[data-page]'), { attributes: true })
if (!canvas.closest('[data-page]').hidden) start()
