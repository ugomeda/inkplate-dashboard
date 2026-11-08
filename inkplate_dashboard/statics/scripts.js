function alignImageToLineGrid(article) {
    const image = article.querySelector("p img");
    const paragraph = article.querySelector("p");

    if (!image || !paragraph)
        return;

    // Reset to CSS/intrinsic sizing for this font-size iteration.
    image.style.width = "";
    image.style.height = "";

    const lineHeight = parseFloat(
        getComputedStyle(paragraph).lineHeight
    );

    const style = getComputedStyle(image);
    const rect = image.getBoundingClientRect();

    const marginTop = parseFloat(style.marginTop);
    const marginBottom = parseFloat(style.marginBottom);

    const borderTop = parseFloat(style.borderTopWidth);
    const borderBottom = parseFloat(style.borderBottomWidth);

    // rect.height already includes the borders.
    const outerHeight = marginTop + rect.height + marginBottom;

    // Snap down to a complete number of text lines.
    const lines = Math.floor(outerHeight / lineHeight);
    const targetOuterHeight = lines * lineHeight;

    // CSS `height` is content height because box-sizing is content-box.
    const targetContentHeight =
        targetOuterHeight
        - marginTop
        - marginBottom
        - borderTop
        - borderBottom;

    if (targetContentHeight <= 0)
        return;

    const ratio = image.naturalWidth / image.naturalHeight;

    image.style.height = `${targetContentHeight}px`;
    image.style.width = `${targetContentHeight * ratio}px`;
}


function fitNewsColumn(newsColumnSelector) {
    const column = document.querySelector(newsColumnSelector);
    const articles = column.querySelectorAll("article");

    for (let size = 30; size >= 14; size -= 0.25) {
        column.style.fontSize = `${size}px`;
        articles.forEach(alignImageToLineGrid);
        if (column.scrollHeight <= column.clientHeight) {
            return;
        }
    }
}