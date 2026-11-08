function fitNewsColumn(newsColumnSelector) {
    const column = document.querySelector(newsColumnSelector);
    const articles = column.querySelectorAll("article");

    for (let size = 30; size >= 14; size -= 0.25) {
        column.style.fontSize = `${size}px`;

        // Image dimensions depend on this font size / line-height.
        // articles.forEach(alignImageToLineGrid);

        // getBoundingClientRect() above and scrollHeight here both force
        // layout, so we're checking the final reflowed geometry.
        if (column.scrollHeight <= column.clientHeight) {
            return;
        }
    }
}