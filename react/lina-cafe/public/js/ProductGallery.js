// Product picture gallery (display/shop/product.h.tt):
//   * thumbnail click  -> swap the main image AND open the lightbox
//   * main image click -> open the lightbox at the current picture
//   * lightbox         -> prev / next / close, walking the thumbnails
// Progressive enhancement: without JS every picture is still visible in the
// thumbnail grid and the page works as a plain document.
(function () {
  "use strict";
  var gallery = document.querySelector("[data-gallery]");
  if (!gallery) return;
  var main = gallery.querySelector("[data-gallery-main]");
  var dialog = document.getElementById("gallery-lightbox");
  if (!main || !dialog || typeof dialog.showModal !== "function") return;
  var zoom = dialog.querySelector("[data-gallery-zoom]");
  var caption = dialog.querySelector("[data-gallery-caption]");
  var thumbs = Array.prototype.slice.call(
    gallery.querySelectorAll("[data-gallery-thumb]")
  );
  if (!zoom || thumbs.length === 0) return;

  var index = 0;

  function srcOf(thumb) {
    return thumb.getAttribute("data-full") || "";
  }
  function altOf(thumb) {
    var img = thumb.querySelector("img");
    return img ? img.getAttribute("alt") || "" : "";
  }
  function show(i) {
    index = (i + thumbs.length) % thumbs.length;
    var src = srcOf(thumbs[index]);
    var alt = altOf(thumbs[index]);
    main.setAttribute("src", src);
    main.setAttribute("alt", alt);
    zoom.setAttribute("src", src);
    zoom.setAttribute("alt", alt);
    if (caption) caption.textContent = alt;
  }
  function open(i) {
    show(i);
    if (!dialog.open) dialog.showModal();
  }

  thumbs.forEach(function (thumb, i) {
    thumb.addEventListener("click", function () {
      open(i);
    });
  });
  main.addEventListener("click", function () {
    open(index);
  });

  var prev = dialog.querySelector("[data-gallery-prev]");
  var next = dialog.querySelector("[data-gallery-next]");
  var close = dialog.querySelector("[data-gallery-close]");
  if (prev) prev.addEventListener("click", function () { open(index - 1); });
  if (next) next.addEventListener("click", function () { open(index + 1); });
  if (close) {
    close.addEventListener("click", function () {
      dialog.close();
    });
  }
  // click on the backdrop (the dialog box itself) closes the overlay
  dialog.addEventListener("click", function (event) {
    if (event.target === dialog) dialog.close();
  });
})();
