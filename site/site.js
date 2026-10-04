"use strict";

// Progressive enhancement: every thumbnail remains a normal image link
// when JavaScript or the native dialog API is unavailable.
const gallery = Array.from(document.querySelectorAll("[data-shot]"));
const dialog = document.getElementById("screenshot-dialog");
if (dialog && typeof dialog.showModal === "function") {
  const image = document.getElementById("lightbox-image");
  const caption = document.getElementById("lightbox-caption");
  let selected = 0;
  let opener = null;

  function show(index) {
    selected = (index + gallery.length) % gallery.length;
    const link = gallery[selected];
    image.src = link.href;
    image.alt = link.querySelector("img").alt;
    caption.textContent = link.dataset.caption;
  }

  gallery.forEach((link, index) => {
    link.addEventListener("click", (event) => {
      if (event.button !== 0 || event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
      event.preventDefault();
      opener = link;
      show(index);
      dialog.showModal();
      document.body.classList.add("viewing-screenshot");
    });
  });
  document.getElementById("previous-shot").addEventListener("click", () => show(selected - 1));
  document.getElementById("next-shot").addEventListener("click", () => show(selected + 1));
  dialog.addEventListener("keydown", (event) => {
    if (event.key === "ArrowLeft" || event.key === "ArrowRight") {
      event.preventDefault();
      show(selected + (event.key === "ArrowLeft" ? -1 : 1));
    }
  });
  dialog.addEventListener("click", (event) => {
    if (event.target === dialog) dialog.close();
  });
  dialog.addEventListener("close", () => {
    document.body.classList.remove("viewing-screenshot");
    if (opener && opener.isConnected) opener.focus();
  });
}
