// Photo upload controller (vanilla JS, no framework).
//
// Wires every wrapper marked [data-img-upload] that contains a file input:
//   1. img-icon button   - the raw <input type=file> is visually hidden and
//      driven by a labelled button with an image icon (label click opens
//      the native picker; no custom file-dialog code).
//   2. live preview      - the picked file appears immediately in the
//      circular frame next to the button (object-cover, avatar shaped),
//      with its name/size underneath.
//   3. compression       - modelled on chiragvaze/Image-Compressor
//      (script.js): canvas resize to max 1920px, quality 0.82,
//      canvas.toBlob -> the compressed File replaces input.files through a
//      DataTransfer, so the multipart POST carries the small bytes. GIFs
//      are skipped (compression would flatten the animation).
//   4. crop modal        - square viewport, drag to pan, zoom slider
//      (wheel also zooms). Apply draws the crop region onto a 512x512
//      canvas and again replaces input.files. The server is unchanged:
//      save_uploaded_image() still magic-byte-checks and ffmpeg-converts
//      to webp (src/http/utils.c).
//
// One lazily-built modal per input; DOM is created here so the templates
// (display/user*.tt) carry only the wrapper markup and nothing else.

const IU_CROP_SIZE = 512;  // crop output: square edge, px
const IU_CROP_QUALITY = 0.82;  // crop output: canvas.toBlob quality

// Server upload cap: MAX_UPLOAD_IMAGE_SIZE in src/http/utils.h (2 MB), minus
// a margin for the multipart envelope + the other form fields. The attempt
// list walks quality/dimensions down until the blob fits - without it a
// noisy photo at 1920px/q0.82 can exceed the cap and the POST is rejected
// server-side AFTER all this client work.
const IU_MAX_BYTES = 2 * 1024 * 1024 - 64 * 1024;
const IU_ATTEMPTS = [
    [1920, 0.82], [1600, 0.72], [1280, 0.62],
    [1024, 0.52], [800, 0.45], [640, 0.40],
];

function iuFmtSize(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(0) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

// Replace the input's files with exactly one (compressed/cropped) file.
// DataTransfer is the only way to assign to input.files from script.
function iuSetFile(input, file) {
    const dt = new DataTransfer();
    dt.items.add(file);
    input.files = dt.files;
}

// Compress `file` on a canvas. Walks IU_ATTEMPTS down until the blob is
// under the server's upload cap; resolves with a smaller File, or null when
// the original should be kept (GIF, decode failure, no size gain).
function iuCompress(file) {
    if (file.type === 'image/gif') return Promise.resolve(null);
    return new Promise((resolve) => {
        const url = URL.createObjectURL(file);
        const img = new Image();
        img.onload = () => {
            URL.revokeObjectURL(url);
            const iw = img.naturalWidth, ih = img.naturalHeight;
            const png = file.type === 'image/png';
            const mime = png ? 'image/png' : 'image/jpeg';
            const canvas = document.createElement('canvas');
            const ctx = canvas.getContext('2d');

            const attempt = (i) => {
                if (i >= IU_ATTEMPTS.length) { resolve(null); return; }
                const [maxDim, quality] = IU_ATTEMPTS[i];
                const scale = Math.min(1, maxDim / Math.max(iw, ih));
                canvas.width = Math.max(1, Math.round(iw * scale));
                canvas.height = Math.max(1, Math.round(ih * scale));
                ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
                canvas.toBlob((blob) => {
                    if (!blob) { resolve(null); return; }
                    if (blob.size > IU_MAX_BYTES) { attempt(i + 1); return; }
                    // Fits - but only swap when it actually got smaller.
                    if (blob.size >= file.size) { resolve(null); return; }
                    const stem = file.name.replace(/\.[^.]*$/, '');
                    resolve(new File([blob], stem + (png ? '.png' : '.jpg'),
                                     { type: blob.type }));
                }, mime, quality);
            };
            attempt(0);
        };
        img.onerror = () => { URL.revokeObjectURL(url); resolve(null); };
        img.src = url;
    });
}

// -- crop modal -------------------------------------------------------------

function iuBuildModal(state) {
    const m = document.createElement('div');
    m.className = 'fixed inset-0 z-50 hidden';
    m.innerHTML =
      '<div class="absolute inset-0 bg-black/60" data-iu-backdrop></div>'
    + '<div class="absolute inset-0 flex items-center justify-center p-4 pointer-events-none">'
    + '  <div class="pointer-events-auto bg-white dark:bg-gray-800 rounded-lg p-4 shadow-xl w-full max-w-sm">'
    + '    <h3 class="text-sm font-semibold text-gray-800 dark:text-white mb-2">Crop photo</h3>'
    + '    <div data-iu-viewport class="relative overflow-hidden w-full aspect-square rounded '
    +      'bg-gray-100 dark:bg-gray-700 cursor-move touch-none select-none">'
    + '      <img data-iu-img alt="" class="absolute left-0 top-0 origin-top-left max-w-none pointer-events-none" draggable="false">'
    + '    </div>'
    + '    <label class="flex items-center gap-2 text-xs text-gray-600 dark:text-gray-300 mt-2">Zoom'
    + '      <input data-iu-zoom type="range" min="100" max="400" value="100" class="flex-1">'
    + '    </label>'
    + '    <div class="flex justify-end gap-2 mt-3">'
    + '      <button type="button" data-iu-cancel class="px-3 py-1.5 rounded text-sm '
    +       'bg-gray-500 hover:bg-gray-600 text-white">Cancel</button>'
    + '      <button type="button" data-iu-apply class="px-3 py-1.5 rounded text-sm '
    +       'bg-indigo-600 hover:bg-indigo-700 text-white">Apply</button>'
    + '    </div>'
    + '  </div>'
    + '</div>';
    document.body.appendChild(m);

    state.modal = m;
    state.viewport = m.querySelector('[data-iu-viewport]');
    state.img = m.querySelector('[data-iu-img]');
    state.zoom = m.querySelector('[data-iu-zoom]');

    m.querySelector('[data-iu-backdrop]').addEventListener('click', iuCloseModal.bind(null, state));
    m.querySelector('[data-iu-cancel]').addEventListener('click', iuCloseModal.bind(null, state));
    m.querySelector('[data-iu-apply]').addEventListener('click', () => iuApplyCrop(state));
    state.zoom.addEventListener('input', () => iuSetZoom(state, state.zoom.value / 100));

    // Drag to pan: the pointer position is tracked against the transform at
    // drag start, so a mid-drag zoom (wheel/slider) cannot jump the image.
    state.viewport.addEventListener('pointerdown', (e) => {
        e.preventDefault();
        // drag state first, capture second: a synthetic PointerEvent (tests)
        // has no active pointer, and setPointerCapture would throw before
        // state.drag was ever set.
        state.drag = { x: e.clientX, y: e.clientY, tx: state.tx, ty: state.ty };
        try { state.viewport.setPointerCapture(e.pointerId); } catch (err) { /* synthetic */ }
    });
    state.viewport.addEventListener('pointermove', (e) => {
        if (!state.drag) return;
        state.tx = state.drag.tx + (e.clientX - state.drag.x);
        state.ty = state.drag.ty + (e.clientY - state.drag.y);
        iuClamp(state);
        iuPaint(state);
    });
    state.viewport.addEventListener('pointerup', () => { state.drag = null; });
    state.viewport.addEventListener('pointercancel', () => { state.drag = null; });
    state.viewport.addEventListener('wheel', (e) => {
        e.preventDefault();
        const next = Math.min(400, Math.max(100,
            Math.round(state.zoom.value + (e.deltaY < 0 ? 10 : -10))));
        state.zoom.value = next;
        iuSetZoom(state, next / 100);
    }, { passive: false });

    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && !m.classList.contains('hidden')) iuCloseModal(state);
    });
    return m;
}

// Zoom is stored relative to the "cover" scale (z=1 = viewport just filled).
function iuSetZoom(state, z) {
    const s = state.cover * z;
    // Keep the point currently at the viewport centre pinned there.
    const cx = (state.vp / 2 - state.tx) / state.s;
    const cy = (state.vp / 2 - state.ty) / state.s;
    state.s = s;
    state.tx = state.vp / 2 - cx * s;
    state.ty = state.vp / 2 - cy * s;
    iuClamp(state);
    iuPaint(state);
}

// The image must always cover the square viewport (no empty edges).
function iuClamp(state) {
    const minX = state.vp - state.iw * state.s;
    const minY = state.vp - state.ih * state.s;
    state.tx = Math.min(0, Math.max(minX, state.tx));
    state.ty = Math.min(0, Math.max(minY, state.ty));
}

function iuPaint(state) {
    state.img.style.transform =
        'translate(' + state.tx + 'px,' + state.ty + 'px) scale(' + state.s + ')';
}

function iuOpenModal(state) {
    if (!state.original) return;
    const m = state.modal || iuBuildModal(state);
    state.img.onload = () => {
        // Un-hide FIRST: while the modal is display:none the viewport
        // measures 0px, cover becomes 0 and the image paints at scale(0)
        // (invisible, unzoomable, unpannable - found by img_upload_test).
        m.classList.remove('hidden');
        state.vp = state.viewport.clientWidth;
        state.iw = state.img.naturalWidth;
        state.ih = state.img.naturalHeight;
        state.cover = Math.max(state.vp / state.iw, state.vp / state.ih);
        state.zoom.value = 100;
        state.s = state.cover;
        state.tx = (state.vp - state.iw * state.s) / 2;
        state.ty = (state.vp - state.ih * state.s) / 2;
        iuClamp(state);
        iuPaint(state);
    };
    state.img.src = state.url;  // crop from the ORIGINAL, pre-compression file
}

function iuCloseModal(state) {
    if (state.modal) state.modal.classList.add('hidden');
    state.drag = null;
}

// Map the visible square back to source pixels and draw it out at
// IU_CROP_SIZE x IU_CROP_SIZE.
function iuApplyCrop(state) {
    const vp = state.viewport.clientWidth;
    const sx = -state.tx / state.s;
    const sy = -state.ty / state.s;
    const sw = vp / state.s;
    const canvas = document.createElement('canvas');
    canvas.width = IU_CROP_SIZE;
    canvas.height = IU_CROP_SIZE;
    canvas.getContext('2d').drawImage(
        state.img, sx, sy, sw, sw, 0, 0, IU_CROP_SIZE, IU_CROP_SIZE);
    const png = state.original.type === 'image/png';
    canvas.toBlob((blob) => {
        if (!blob) { iuCloseModal(state); return; }
        const stem = state.original.name.replace(/\.[^.]*$/, '');
        const file = new File([blob], stem + (png ? '.png' : '.jpg'),
                              { type: blob.type });
        iuSetFile(state.input, file);
        iuShowPreview(state, URL.createObjectURL(blob));
        state.nameEl.textContent =
            file.name + ' · cropped ' + iuFmtSize(file.size);
        iuCloseModal(state);
    }, png ? 'image/png' : 'image/jpeg', IU_CROP_QUALITY);
}

// -- per-wrapper wiring -----------------------------------------------------

function iuShowPreview(state, url) {
    if (state.url && state.url !== url && state.url.startsWith('blob:')) {
        URL.revokeObjectURL(state.url);
    }
    state.url = url;
    state.preview.src = url;
    state.preview.classList.remove('hidden');
    state.placeholder.classList.add('hidden');
}

function iuReset(state) {
    if (state.url && state.url !== url && state.url.startsWith('blob:')) {
        URL.revokeObjectURL(state.url);
    }
    state.url = null;
    state.original = null;
    state.preview.removeAttribute('src');
    state.preview.classList.add('hidden');
    state.placeholder.classList.remove('hidden');
    state.cropBtn.classList.add('hidden');
    state.nameEl.textContent = 'no file selected';
}

function iuOnSelect(state) {
    const file = state.input.files && state.input.files[0];
    if (!file) { iuReset(state); return; }

    const seq = ++state.seq;
    state.original = file;
    iuShowPreview(state, URL.createObjectURL(file));
    state.nameEl.textContent = file.name + ' · ' + iuFmtSize(file.size);

    if (file.type === 'image/gif') {
        // Animated: compression/cropping would freeze it on one frame.
        state.cropBtn.classList.add('hidden');
        state.nameEl.textContent += ' (animated, kept as-is)';
        return;
    }
    state.cropBtn.classList.remove('hidden');

    iuCompress(file).then((small) => {
        // A newer selection may have landed while the canvas worked.
        if (!small || seq !== state.seq || !state.input.files[0]) return;
        iuSetFile(state.input, small);
        state.nameEl.textContent =
            file.name + ' · ' + iuFmtSize(file.size) +
            ' → ' + iuFmtSize(small.size);
    });
}

function iuWire(wrapper) {
    if (wrapper.dataset.iuReady) return;
    const input = wrapper.querySelector('input[type="file"]');
    if (!input) return;
    wrapper.dataset.iuReady = '1';

    const state = {
        input,
        wrapper,
        preview: wrapper.querySelector('[data-img-preview]'),
        placeholder: wrapper.querySelector('[data-img-placeholder]'),
        cropBtn: wrapper.querySelector('[data-img-crop]'),
        nameEl: wrapper.querySelector('[data-img-name]'),
        modal: null, viewport: null, img: null, zoom: null,
        url: null, original: null, seq: 0,
        tx: 0, ty: 0, s: 1, cover: 1, vp: 1, iw: 1, ih: 1, drag: null,
    };
    if (!state.preview || !state.cropBtn || !state.nameEl) return;

    const existingSrc = state.preview.getAttribute('src');
    if (existingSrc && existingSrc.trim() !== '') {
        state.url = existingSrc;
        state.preview.classList.remove("hidden");
        if (state.placeholder) state.placeholder.classList.add("hidden");
        if (state.nameEl.textContent.trim() === 'no file selected' ||
            state.nameEl.textContent.trim() === 'no file') {
            state.nameEl.textContent = 'current photo';
        }
    }

    input.addEventListener('change', () => iuOnSelect(state));
    state.cropBtn.addEventListener('click', () => iuOpenModal(state));
}

function iuInit() {
    document.querySelectorAll('[data-img-upload]').forEach(iuWire);
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', iuInit);
} else {
    iuInit();
}
document.addEventListener('md:swapped', iuInit);
