function fitNewsColumn(newsColumnSelector) {
    const column = document.querySelector(newsColumnSelector);

    for (let size = 30; size >= 14; size -= 0.25) {
        column.style.fontSize = `${size}px`;
        if (column.scrollHeight <= column.clientHeight) {
            return;
        }
    }
}