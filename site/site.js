/**
 * The VST Studio home page's behaviour.  Everything here is an enhancement: with no JavaScript, or if GitHub's API
 * can't be reached, every download link still goes to the releases/latest page and the page reads top to bottom.
 *
 *   1. Downloads.  Release file names carry the version (VSTStudio-2.0.0-windows-x64-setup.exe), so there's no
 *      stable "latest" URL per file.  We ask GitHub's public API for the latest release once per visit (cached in
 *      sessionStorage; the unauthenticated limit is 60 requests an hour per address, plenty for a page view) and point
 *      each button at its file by matching the end of the name (data-asset="windows-x64-setup.exe").
 *   2. "Your computer": the visitor's OS picks the hero button's label and target and highlights its card.  Phones
 *      and tablets get the companion demo instead, since VST Studio itself runs on a computer.
 *   3. Linux: distro + processor pick the .deb, and the apt command names that exact file.
 *   4. Quick-start stepper, phone screenshot carousel, nav shadow, reveal-on-scroll and the HUD count-up.
 */
(() => {
  'use strict';
  const REPO = 'joekiller/VSTStudio';
  const LATEST_PAGE = `https://github.com/${REPO}/releases/latest`;
  const CACHE_KEY = 'vst-site-latest';
  const $ = (s, el = document) => el.querySelector(s);
  const $$ = (s, el = document) => [...el.querySelectorAll(s)];
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  document.documentElement.classList.add('js');

  // ---------------------------------------------------------------- OS detection
  /** 'windows' | 'mac' | 'linux' | 'mobile' | null.  iPadOS reports itself as a Mac, so touch points tell them apart. */
  function detectOS() {
    const ua = navigator.userAgent;
    const plat = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || '';
    if (/Android|iPhone|iPod|iPad/i.test(ua) || (/Mac/i.test(plat) && navigator.maxTouchPoints > 1)) return 'mobile';
    if (/Win/i.test(plat) || /Windows/i.test(ua)) return 'windows';
    if (/Mac/i.test(plat) || /Mac OS X/i.test(ua)) return 'mac';
    if (/Linux|X11|CrOS/i.test(plat + ua)) return 'linux';
    return null;
  }
  /** Raspberry Pis and ARM laptops say aarch64/arm in the UA; everything else on Linux is assumed amd64. */
  const linuxArch = /aarch64|arm64|armv8/i.test(navigator.userAgent + navigator.platform) ? 'arm64' : 'amd64';
  const os = detectOS();

  // ---------------------------------------------------------------- latest release
  let release = null;   // { tag, version, date, assets: [{ name, url, size }] }

  async function latestRelease() {
    try {
      const hit = JSON.parse(sessionStorage.getItem(CACHE_KEY) || 'null');
      if (hit && Date.now() - hit.at < 10 * 60 * 1000) return hit.release;
    } catch { /* storage blocked: just fetch */ }
    const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, { headers: { Accept: 'application/vnd.github+json' } });
    if (!res.ok) throw new Error(`GitHub said ${res.status}`);
    const j = await res.json();
    const r = {
      tag: j.tag_name, version: String(j.tag_name || '').replace(/^v/, ''), date: j.published_at,
      assets: (j.assets || []).map((a) => ({ name: a.name, url: a.browser_download_url, size: a.size })),
    };
    try { sessionStorage.setItem(CACHE_KEY, JSON.stringify({ at: Date.now(), release: r })); } catch { /* fine */ }
    return r;
  }

  const findAsset = (suffix) => release && release.assets.find((a) => a.name.endsWith(suffix));
  const mb = (bytes) => `${Math.round(bytes / 1e6)} MB`;

  /** Points one link at the release file ending in `suffix`; leaves it on the releases page if there isn't one. */
  function wire(a, suffix) {
    const asset = findAsset(suffix);
    if (!asset) { a.href = LATEST_PAGE; a.removeAttribute('title'); return null; }
    a.href = asset.url;
    a.title = `${asset.name} · ${mb(asset.size)}`;
    return asset;
  }

  // ---------------------------------------------------------------- Linux picker
  const distro = $('#distro');
  let arch = linuxArch;
  // Ubuntu says so in some UAs; a Pi (arm64) most likely runs Raspberry Pi OS, which is Debian.
  if (/Ubuntu/i.test(navigator.userAgent)) distro.value = 'ubuntu-24.04';
  else if (os === 'linux' && arch === 'arm64') distro.value = 'debian-bookworm';

  function paintArch() {
    $$('#arch button').forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.arch === arch)));
  }
  function paintDeb() {
    const deb = $('#deb');
    const asset = wire(deb, `_${distro.value}_${arch}.deb`);
    deb.textContent = asset ? `Download .deb · ${mb(asset.size)}` : 'Download .deb';
    $('#deb-cmd').textContent = asset ? `sudo apt install ./${asset.name}` : 'sudo apt install ./vststudio_*.deb';
  }
  $('#arch').addEventListener('click', (e) => {
    const b = e.target.closest('[data-arch]');
    if (b) { arch = b.dataset.arch; paintArch(); paintDeb(); }
  });
  distro.addEventListener('change', paintDeb);
  paintArch();

  $$('.copy').forEach((btn) => btn.addEventListener('click', async () => {
    const text = $(`#${btn.dataset.copy}`).textContent;
    try { await navigator.clipboard.writeText(text); btn.textContent = 'Copied'; } catch { btn.textContent = 'Select it'; }
    setTimeout(() => { btn.textContent = 'Copy'; }, 1600);
  }));

  // ---------------------------------------------------------------- hero + cards for this visitor
  const HERO = {
    windows: { label: 'Download for Windows', asset: 'windows-x64-setup.exe' },
    mac: { label: 'Download for Mac', asset: 'macos-arm64.dmg' },
    linux: { label: 'Download for Linux', asset: null },   // the right .deb depends on the picker, so scroll to it
  };
  if (os && os !== 'mobile') {
    const card = $(`.dl[data-os="${os}"]`);
    card.classList.add('you');
    $('.badge-you', card).hidden = false;
    $('#hero-dl-label').textContent = HERO[os].label;
    if (os === 'linux') $('#hero-dl').href = '#download';
  } else if (os === 'mobile') {
    // VST Studio runs on a computer; on a phone the best first taste is the companion demo, and the downloads stay
    // one tap away for sending to yourself.
    $('#hero-dl').href = '#download';
    $('#hero-dl-label').textContent = 'Get it for your computer';
  }

  function paintDownloads() {
    $$('[data-asset]').forEach((a) => {
      const asset = wire(a, a.dataset.asset);
      if (asset && a.classList.contains('btn')) a.textContent = `${a.textContent.split(' · ')[0]} · ${mb(asset.size)}`;
    });
    paintDeb();
    if (os && HERO[os] && HERO[os].asset) wire($('#hero-dl'), HERO[os].asset);
    const when = new Date(release.date).toLocaleDateString(undefined, { year: 'numeric', month: 'long', day: 'numeric' });
    $('#dl-version').textContent = `Version ${release.version}, released ${when}.`;
    const meta = $('#hero-meta');
    meta.textContent = '';
    meta.append(`Version ${release.version} · `);
    const more = document.createElement('a');
    more.href = '#download';
    more.textContent = os === 'mac' ? 'Apple silicon Macs; other systems' : 'Mac, Linux, Raspberry Pi and more';
    meta.append(more, ' · free, nothing to sign up for.');
  }

  latestRelease().then((r) => { release = r; paintDownloads(); }).catch(() => {
    // No API (offline, rate-limited, blocked): the links already go to the release page, which lists every file.
    $('#dl-version').textContent = 'Each button opens the latest release; pick the file named on the card.';
  });

  // ---------------------------------------------------------------- quick-start stepper
  const tabs = $$('#steps [role="tab"]');
  const shot = $('#step-img');
  /** Shows step i's screenshot; arrow keys move between steps (the usual tablist keys). */
  function select(i, focus) {
    tabs.forEach((t, k) => { t.setAttribute('aria-selected', String(k === i)); t.tabIndex = k === i ? 0 : -1; });
    const t = tabs[i];
    if (focus) t.focus();
    if (shot.getAttribute('src') === t.dataset.img) return;
    const swap = () => { shot.src = t.dataset.img; shot.alt = t.dataset.alt; shot.classList.remove('swap'); };
    if (reduced) { swap(); return; }
    shot.classList.add('swap');
    setTimeout(swap, 180);
  }
  tabs.forEach((t, i) => {
    t.tabIndex = i === 0 ? 0 : -1;
    t.addEventListener('click', () => select(i));
    t.addEventListener('keydown', (e) => {
      const d = { ArrowDown: 1, ArrowRight: 1, ArrowUp: -1, ArrowLeft: -1 }[e.key];
      if (d) { e.preventDefault(); select((i + d + tabs.length) % tabs.length, true); }
      if (e.key === 'Home') { e.preventDefault(); select(0, true); }
      if (e.key === 'End') { e.preventDefault(); select(tabs.length - 1, true); }
    });
  });
  // Warm the cache so switching steps is instant.
  addEventListener('load', () => tabs.forEach((t) => { const im = new Image(); im.src = t.dataset.img; }));

  // ---------------------------------------------------------------- phone carousel
  const screens = $('#phone-screens');
  const shots = $$('img', screens);
  const dots = $('#phone-dots');
  shots.forEach((im, i) => {
    const b = document.createElement('button');
    b.type = 'button';
    b.setAttribute('aria-label', `Screenshot ${i + 1}`);
    b.addEventListener('click', () => { go(i); stop(); });
    dots.append(b);
  });
  dots.removeAttribute('aria-hidden');
  let at = 0;
  const go = (i) => screens.scrollTo({ left: i * screens.clientWidth, behavior: reduced ? 'auto' : 'smooth' });
  const paintDots = () => $$('button', dots).forEach((b, k) => b.setAttribute('aria-current', String(k === at)));
  screens.addEventListener('scroll', () => {
    const i = Math.round(screens.scrollLeft / Math.max(1, screens.clientWidth));
    if (i !== at) { at = i; paintDots(); }
  }, { passive: true });
  paintDots();
  // Auto-advance only while it's on screen, and never after the visitor takes over (touch, wheel, keys, a dot).
  let timer = null;
  const stop = () => { clearInterval(timer); timer = null; stopped = true; };
  let stopped = reduced;
  ['pointerdown', 'wheel', 'keydown'].forEach((ev) => screens.addEventListener(ev, stop, { passive: true }));

  // ---------------------------------------------------------------- scroll effects
  const nav = $('#nav');
  const onScroll = () => nav.classList.toggle('scrolled', scrollY > 8);
  addEventListener('scroll', onScroll, { passive: true });
  onScroll();

  /** Counts a HUD number up from zero the first time it is seen. */
  function countUp(el) {
    const end = Number(el.dataset.count); const suffix = el.dataset.suffix || '';
    if (reduced) return;
    const t0 = performance.now(); const dur = 1400;
    const step = (t) => {
      const p = Math.min(1, (t - t0) / dur);
      el.textContent = Math.round(end * (1 - Math.pow(1 - p, 3))) + suffix;
      if (p < 1) requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
  }

  if ('IntersectionObserver' in window) {
    const io = new IntersectionObserver((entries) => entries.forEach((e) => {
      if (!e.isIntersecting) return;
      e.target.classList.add('in');
      if (e.target.dataset.count) countUp(e.target);
      io.unobserve(e.target);
    }), { threshold: 0.15 });
    $$('.reveal, [data-count]').forEach((el) => io.observe(el));

    new IntersectionObserver(([e]) => {
      if (e.isIntersecting && !stopped && !timer) timer = setInterval(() => go((at + 1) % shots.length), 3200);
      else if (!e.isIntersecting && timer) { clearInterval(timer); timer = null; }
    }, { threshold: 0.5 }).observe(screens);
  } else {
    $$('.reveal').forEach((el) => el.classList.add('in'));
  }
})();
