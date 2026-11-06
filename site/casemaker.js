// The case, made for your board: case/lilfm1buddy-case.scad run in the browser (OpenSCAD in WebAssembly), shown in 3D
// (three.js: drag to turn it) and its STLs to download. The file is the repo's, copied next to the page (tools/site.sh),
// or from GitHub. OpenSCAD and three.js load once the case is on screen.

const OPENSCAD = 'https://cdn.jsdelivr.net/npm/openscad-wasm-prebuilt@1.2.0/dist/openscad.js'
const SCAD = ['fw/lilfm1buddy-case.scad', 'https://raw.githubusercontent.com/konsumer/lilfm1buddy/main/case/lilfm1buddy-case.scad']

// the boards (everything about them, measured from their makers' CAD, is in the .scad's own table). battery: whether
// it charges one; room: how wide a battery can be under it (the .scad says so too)
const BOARDS = [
  { id: 'xiaoplus', name: 'XIAO ESP32S3 Plus', battery: true, room: 14.5 },
  { id: 'xiao', name: 'XIAO ESP32S3', battery: true, room: 14.5 },
  { id: 'zero', name: 'Waveshare ESP32-S3-Zero', battery: false }
]
// flat LiPos: across, along, thick (the name is thickness, width, length: 401230 is 4 x 12 x 30 mm)
const BATTERIES = [
  { name: 'No battery', size: null },
  { name: '301230 · 110 mAh', size: [12, 30, 3] },
  { name: '401230 · 150 mAh', size: [12, 30, 4] },
  { name: '501230 · 200 mAh', size: [12, 30, 5] },
  { name: 'Another size…', size: 'custom' }
]
const ROOM = [0.6, 1.5, 0.4] // added to a battery's size: its tolerance, and its leads at one end

// the pieces of the view, their colours (the page's), and how far apart they're drawn (to see inside)
const PIECES = [
  { part: 'view_base', color: 0x36b37e, z: -10 },
  { part: 'view_board', color: 0x26262b, z: 0 },
  { part: 'view_battery', color: 0xb8bcc4, z: -10, battery: true },
  { part: 'view_shell', color: 0xff8a3d, z: 16 },
  { part: 'view_buttons', color: 0xf4f4f4, z: 16 }
]

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
        .replace(/^Assertion .*? failed: /, '')
        .replace(/ in file .*$/, '')
        .replace(/^"(.*)"$/, '$1')
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
    set(name, stl, color, z = 0) {
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
      mesh.position.z = z
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
  const custom = $('.maker-custom')
  const sizes = ['x', 'y', 'z'].map((n) => $(`[name=bat_${n}]`))
  const antennaBox = $('[name=antenna]')
  const antennaRow = $('.maker-antenna')
  const status = $('.maker-status')
  const canvas = $('canvas')
  const buttons = {}
  for (const b of box.querySelectorAll('.maker-download')) buttons[b.dataset.part] = b
  let view, busy, again, timer
  const urls = []

  const say = (text, bad) => {
    status.textContent = text
    status.className = `note maker-status${bad ? ' bad' : ''}`
  }
  const board = () => BOARDS.find((b) => b.id === boardSel.value) || BOARDS[0]
  const nm = (v) => +(+v).toFixed(2)

  // the battery picked (its own size, without the room), or null; or an error
  const battery = () => {
    if (!board().battery) {
      return { size: null }
    }
    const p = BATTERIES[+batterySel.value] || BATTERIES[0]
    if (p.size !== 'custom') {
      return { size: p.size, name: p.name }
    }
    const size = sizes.map((i) => parseFloat(i.value))
    if (size.some((v) => !(v > 0))) {
      return { error: 'Fill in the battery’s width, length and thickness.' }
    }
    return { size, name: size.join(' × ') + ' mm' }
  }

  const defines = (part, bat) => {
    const d = { part: `"${part}"`, board: `"${board().id}"`, battery: bat ? 'true' : 'false', antenna: antennaBox.checked ? 'true' : 'false' }
    if (bat) {
      d.bat = `[${bat.map((v, k) => nm(v + ROOM[k])).join(', ')}]`
    }
    return d
  }

  const name = (part, bat) => `lilfm1buddy-case-${board().id}${bat ? `-battery-${bat.join('x')}` : ''}-${part}.stl`

  // make the case for what's picked; once at a time, the last change wins
  const make = async () => {
    if (busy) {
      again = true
      return
    }
    const bat = battery()
    for (const b of Object.values(buttons)) {
      b.removeAttribute('href')
      b.setAttribute('aria-disabled', 'true')
    }
    if (bat.error) {
      say(bat.error, true)
      return
    }
    busy = true
    say(openscad ? 'Making it…' : 'Getting OpenSCAD (about 11 MB, once)…')
    try {
      view ||= await viewer(canvas)
      // the files first: if the model refuses (a battery too wide), that's the message
      while (urls.length) URL.revokeObjectURL(urls.pop())
      for (const part of ['shell', 'base', 'buttons']) {
        const stl = await render(defines(part, bat.size))
        const url = URL.createObjectURL(new Blob([stl], { type: 'model/stl' }))
        urls.push(url)
        buttons[part].href = url
        buttons[part].download = name(part, bat.size)
      }
      for (const p of PIECES) {
        const stl = p.battery && !bat.size ? null : await render(defines(p.part, bat.size))
        view.set(p.part, stl, p.color, p.z)
      }
      view.fit()
      canvas.hidden = false
      for (const b of Object.values(buttons)) b.removeAttribute('aria-disabled')
      say(bat.size ? `With a ${bat.name} battery under the board. Drag it to turn it.` : 'Drag it to turn it: the top and the base are drawn apart, to see inside.')
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
  const changed = () => {
    const b = board()
    batterySel.disabled = !b.battery
    if (!b.battery) {
      batterySel.value = '0'
    }
    custom.hidden = !(b.battery && BATTERIES[+batterySel.value]?.size === 'custom')
    antennaRow.hidden = !b.battery // (the XIAOs: the ones with an antenna socket)
    clearTimeout(timer)
    timer = setTimeout(make, 300)
  }

  boardSel.innerHTML = BOARDS.map((b) => `<option value="${b.id}">${b.name}</option>`).join('')
  batterySel.innerHTML = BATTERIES.map((b, i) => `<option value="${i}">${b.name}</option>`).join('')
  sizes.forEach((input, k) => (input.value = BATTERIES[2].size[k]))
  $('.maker-room').textContent = `${BOARDS[0].room} mm`
  boardSel.value = 'xiaoplus'
  boardSel.onchange = changed
  batterySel.onchange = changed
  antennaBox.onchange = changed
  for (const i of sizes) i.oninput = changed

  // at the first look at it
  new IntersectionObserver(
    (seen, o) => {
      if (seen.some((s) => s.isIntersecting)) {
        o.disconnect()
        changed()
      }
    },
    { rootMargin: '300px' }
  ).observe(box)
}
