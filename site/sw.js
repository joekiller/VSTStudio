/**
 * Retires the service worker VST Ride-along used to register at the site root, when Ride-along was the whole site.
 *
 * Phones that opened Ride-along before it moved to /ride/ have a worker at this address whose scope is the whole
 * site, serving its cached page first.  Browsers re-fetch this file when they check for updates; this version
 * clears that old cache and unregisters itself.  It doesn't reload open pages, since someone may be mid-drive with
 * the old Ride-along recording; the next launch loads the home page from the network, and that sends Ride-along's
 * old links and home-screen icons on to /ride/.  The new Ride-along (/ride/sw.js) and the companion
 * (/companion/sw.js) register their own, narrower workers and use their own caches, which this leaves alone.
 * The home page never registers this file itself.
 */
self.addEventListener('install', () => self.skipWaiting());

self.addEventListener('activate', (e) => {
  e.waitUntil((async () => {
    await caches.delete('vst-ride-v1');
    await self.registration.unregister();
  })());
});
