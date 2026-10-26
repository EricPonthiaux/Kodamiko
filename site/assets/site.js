// SPDX-License-Identifier: GPL-3.0-only

// Copies the clone command of the hero; the button shows a check for a moment.
const copiedMs = 1500;

for (const button of document.querySelectorAll(".copy")) {
  button.addEventListener("click", async () => {
    await navigator.clipboard.writeText(button.dataset.command);
    button.classList.add("copied");
    setTimeout(() => button.classList.remove("copied"), copiedMs);
  });
}
