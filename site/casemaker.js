// The case, made for your board: case/lilfm1buddy-case.scad run in the browser (OpenSCAD in WebAssembly), shown in 3D
// (three.js: drag to turn it) and its STLs to download. The file is the repo's, copied next to the page by the pages
// workflow (fw/), or from GitHub. OpenSCAD and three.js load once the case is on screen.

const OPENSCAD = 'https://cdn.jsdelivr.net/npm/openscad-wasm-prebuilt@1.2.0/dist/openscad.js'
const SCAD = ['fw/lilfm1buddy-case.scad', 'https://raw.githubusercontent.com/konsumer/lilfm1buddy/main/case/lilfm1buddy-case.scad']

// the boards the case is made for. Their outline, their switches' places and the cap are in the .scad's own table
// (measured from Seeed's and Waveshare's CAD); here they're only for the battery list, which needs the outline
const BOARDS = [
  { id: 'xiaoplus', name: 'XIAO ESP32S3 Plus', l: 21.15, w: 17.78 },
  { id: 'xiao', name: 'XIAO ESP32S3', l: 21.15, w: 17.78 },
  { id: 'zero', name: 'Waveshare ESP32-S3-Zero', l: 23.52, w: 18.02 }
]
const BATTERIES = [
  { name: 'No battery', size: null },
  { name: '150 mAh · 20 × 15 × 4', size: [20, 15, 4] },
  { name: '180 mAh · 20 × 15 × 5', size: [20, 15, 5] },
  { name: '300 mAh · 25 × 20 × 5', size: [25, 20, 5] },
  { name: '500 mAh · 503035', size: [35, 30, 5] },
  { name: '1000 mAh · 603450', size: [50, 34, 6] }
]
const GAP = 0.4 // (the case's gap around the board: a battery must fit inside it too)

// the pieces of the view, and their colours (the page's)
const BASE = 0x36b37e
const LID = 0xff8a3d
const BOARD = 0x26262b
const BATTERY = 0xb8bcc4

let openscad, scadText

async function scad() {
  if (!scadText) {
    for (const u of SCAD) {
      const r = await fetch(u).catch(() => null)
      if (r && r.ok) {
        scadText = await r.text()
        break
      }
    }
    if (!scadText) {
      throw new Error("couldn't get the case's OpenSCAD file")
    }
  }
  return scadText
}

// one part as an STL. A new OpenSCAD each time: its main runs once (a new one starts in a moment)
async function render(defines) {
  openscad ||= await import(OPENSCAD)
  const log = []
  const o = await openscad.createOpenSCAD({ noInitialRun: true, print: (s) => log.push(s), printErr: (s) => log.push(s) })
  const m = o.getInstance()
  m.FS.writeFile('/case.scad', await scad())
  const args = ['--backend=manifold', '--export-format=binstl', '-o', '/out.stl']
  for (const [k, v] of Object.entries(defines)) {
    args.push('-D', `${k}=${v}`) // (before the file: the wasm build ignores them after it)
  }
  args.push('/case.scad')
  let code
  try {
    code = m.callMain(args)
  } catch (e) {
    code = e?.status ?? 1
  }
  let stl
  try {
    stl = m.FS.readFile('/out.stl')
  } catch {}
  // (the wasm build answers 0 even when the model refused: its refusals are in the log, and the STL is half-made)
  const why = log.find((l) => /^ERROR/.test(l.trim()))
  if (code || !stl || why) {
    throw new Error(
      (why || log.at(-1) || 'OpenSCAD failed')
        .trim()
        .replace(/^ERROR:\s*/, '')
        .replace(/ in file .*$/, '')
    )
  }
  return stl
}

// --------------------------------------------------------------------------------------------- the view ---
async function viewer(canvas) {
  const THREE = await import('three')
  const { OrbitControls } = await import('three/addons/controls/OrbitControls.js')
  const { STLLoader } = await import('three/addons/loaders/STLLoader.js')
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true })
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2))
  const scene = new THREE.Scene()
  const camera = new THREE.PerspectiveCamera(35, 1, 1, 2000)
  camera.up.set(0, 0, 1) // OpenSCAD's z is up
  camera.position.set(0, -200, 100)
  scene.add(new THREE.HemisphereLight(0xffffff, 0x444450, 2.2))
  const sun = new THREE.DirectionalLight(0xffffff, 1.6)
  sun.position.set(-60, -100, 160)
  scene.add(sun)
  const controls = new OrbitControls(camera, canvas)
  controls.enableDamping = true
  controls.enablePan = false
  const group = new THREE.Group()
  scene.add(group)
  const meshes = {}
  const loader = new STLLoader()
  let framed = false

  const size = () => {
    const w = canvas.clientWidth
    const h = canvas.clientHeight
    if (!w || !h) {
      return
    }
    if (canvas.width !== Math.round(w * renderer.getPixelRatio()) || canvas.height !== Math.round(h * renderer.getPixelRatio())) {
      renderer.setSize(w, h, false)
      camera.aspect = w / h
      camera.updateProjectionMatrix()
      if (framed) {
        frame()
      }
    }
  }
  // aim at it all, far enough that its bounding sphere fits however it's turned
  const frame = () => {
    const box = new THREE.Box3().setFromObject(group)
    if (box.isEmpty()) {
      return
    }
    const sphere = box.getBoundingSphere(new THREE.Sphere())
    const fov = (camera.fov * Math.PI) / 180
    const fit = Math.min(fov, 2 * Math.atan(Math.tan(fov / 2) * camera.aspect))
    let dir = camera.position.clone().sub(controls.target).normalize()
    if (!framed || !Number.isFinite(dir.x)) {
      dir = new THREE.Vector3(1.1, -2.3, 1.5).normalize()
    }
    controls.target.copy(sphere.center)
    camera.position.copy(sphere.center).addScaledVector(dir, (sphere.radius / Math.sin(fit / 2)) * 1.04)
    controls.update()
    framed = true
  }
  renderer.setAnimationLoop(() => {
    size()
    controls.update()
    renderer.render(scene, camera)
  })
  return {
    set(name, stl, color) {
      if (meshes[name]) {
        group.remove(meshes[name])
        meshes[name].geometry.dispose()
        delete meshes[name]
      }
      if (!stl) {
        return
      }
      const geo = loader.parse(stl.buffer.slice(stl.byteOffset, stl.byteOffset + stl.byteLength))
      geo.computeVertexNormals()
      const mesh = new THREE.Mesh(geo, new THREE.MeshStandardMaterial({ color, roughness: 0.6, metalness: 0.05 }))
      meshes[name] = mesh
      group.add(mesh)
    },
    fit: frame
  }
}

// ------------------------------------------------------------------------------------------------ page ---
export function init() {
  const box = document.getElementById('maker')
  if (!box) {
    return
  }
  const $ = (s) => box.querySelector(s)
  const boardSel = $('[name=board]')
  const batterySel = $('[name=battery]')
  const nameBox = $('[name=name]')
  const status = $('.maker-status')
  const canvas = $('canvas')
  const buttons = {}
  for (const b of box.querySelectorAll('.maker-download')) buttons[b.dataset.part] = b
  let view,
    busy,
    again,
    made = {}

  const say = (text, bad) => {
    status.textContent = text
    status.className = `note maker-status${bad ? ' bad' : ''}`
  }
  const board = () => BOARDS.find((b) => b.id === boardSel.value) || BOARDS[0]
  const battery = () => BATTERIES[+batterySel.value] || BATTERIES[0]

  // the cap, as big as the board's switches allow: it must not reach RESET's switch (the model checks it too, but a
  // preset that can't be made shouldn't stop the page)
  const cap = (b) => {
    const gap = Math.hypot(b.boot[0] - b.reset[0], b.boot[1] - b.reset[1])
    const most = 2 * (gap - 2.5) - 2
    return Math.max(5, Math.floor(Math.min(b.btn, most) * 2) / 2)
  }
  const nm = (v) => +(+v).toFixed(2)
  const list = (a) => `[${a.map(nm).join(', ')}]`

  // the case's own name: the board, and the battery if there is one
  const name = (part) => {
    const b = board()
    const bat = battery().size
    return `lilfm1buddy-case-${b.id}${bat ? `-battery-${bat.join('x')}` : ''}-${part}.stl`
  }

  // everything the model is made with comes from the board and the battery: the outline, where its switches are, how
  // big its button is (all in BOARDS), and the room under it
  const defines = (part) => {
    const b = board()
    const bat = battery().size
    const d = {
      part: `"${part}"`,
      board: `"${b.id}"`, // (everything else about the board is the .scad's: one place for the measured numbers)
      name: nameBox.checked ? 'true' : 'false',
      battery: bat ? 'true' : 'false'
    }
    if (bat) {
      d.bat = list(bat)
    }
    return d
  }

  // one part of the file, made once for the board and battery it's made for
  const piece = async (part, d, sig) => {
    const key = `${part}|${sig}`
    if (!made[key]) {
      made[key] = await render({ ...d, part: `"${part}"` })
    }
    return made[key]
  }

  // make the case for what's picked; once at a time, the last change wins
  const make = async () => {
    if (busy) {
      again = true
      return
    }
    busy = true
    for (const b of Object.values(buttons)) {
      b.setAttribute('aria-disabled', 'true')
    }
    say(made ? 'Making it…' : 'Getting OpenSCAD (about 11 MB, once)…')
    try {
      view ||= await viewer(canvas)
      made = {}
      const sig = `${board().id}|${battery().size ? battery().size.join('x') : 'none'}|${nameBox.checked}`
      const d = defines('base')
      // the view: the case around a stand-in for the board (and the battery)
      view.set('base', await piece('base', d, sig), BASE)
      view.set('view_board', await piece('view_board', d, sig), BOARD)
      view.set('view_battery', battery().size ? await piece('view_battery', d, sig) : null, BATTERY)
      view.set('view_lid', await piece('view_lid', d, sig), LID)
      view.set('view_button', await piece('view_button', d, sig), LID)
      view.fit()
      canvas.hidden = false
      // what to print: the plate to check the holes, then the case
      for (const part of ['base', 'lid', 'button']) {
        const stl = await piece(part, d, sig)
        const url = URL.createObjectURL(new Blob([stl], { type: 'model/stl' }))
        buttons[part].href = url
        buttons[part].download = name(part)
        buttons[part].removeAttribute('aria-disabled')
      }
      say(battery().size ? `With a ${battery().name.split('·')[0].trim()} battery: the board sits on a shelf over it. Drag it to turn it.` : 'Without a battery. Drag it to turn it.')
    } catch (e) {
      say(`It didn’t work: ${e.message}`, true)
    } finally {
      busy = false
      if (again) {
        again = false
        make()
      }
    }
  }

  // the batteries that fit this board: the rest won't go under it
  const pickBatteries = () => {
    const b = board()
    const was = batterySel.value
    batterySel.innerHTML = BATTERIES.map((bat, i) => {
      const fits = !bat.size || (bat.size[0] <= b.l + 2 * GAP - 0.4 && bat.size[1] <= b.w + 2 * GAP - 0.4)
      return `<option value="${bat.size ? i : 'none'}"${bat.size && !fits ? ' disabled' : ''}>${bat.name}${bat.size && !fits ? ' (too big for this board)' : ''}</option>`
    }).join('')
    batterySel.value = [...batterySel.options].some((o) => o.value === was && !o.disabled) ? was : 'none'
  }

  boardSel.innerHTML = BOARDS.map((b) => `<option value="${b.id}">${b.name}</option>`).join('')
  boardSel.value = 'xiaoplus'
  pickBatteries()
  boardSel.onchange = () => {
    pickBatteries()
    make()
  }
  batterySel.onchange = make
  nameBox.onchange = make

  // at the first look at it
  new IntersectionObserver(
    (seen, o) => {
      if (seen.some((s) => s.isIntersecting)) {
        o.disconnect()
        make()
      }
    },
    { rootMargin: '300px' }
  ).observe(box)
}
