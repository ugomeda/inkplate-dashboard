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


function smoothPath(points, tension = 1) {
    if (points.length === 0)
        return "";

    if (points.length === 1)
        return `M ${points[0].x} ${points[0].y}`;

    let path = `M ${points[0].x} ${points[0].y}`;

    for (let i = 0; i < points.length - 1; i++) {
        const p0 = points[i - 1] ?? points[i];
        const p1 = points[i];
        const p2 = points[i + 1];
        const p3 = points[i + 2] ?? p2;

        const cp1x = p1.x + (p2.x - p0.x) / 6 * tension;
        const cp1y = p1.y + (p2.y - p0.y) / 6 * tension;

        const cp2x = p2.x - (p3.x - p1.x) / 6 * tension;
        const cp2y = p2.y - (p3.y - p1.y) / 6 * tension;

        path += `
            C ${cp1x} ${cp1y}
              ${cp2x} ${cp2y}
              ${p2.x} ${p2.y}
        `;
    }

    return path;
}

function drawTemperatureChart() {
    const container = document.querySelector("#weather-section--hourly-table");
    const svg = document.querySelector("#weather-section--hourly-graph");
    const slots = [...container.querySelectorAll(".weather-section--hourly-slot")];

    const temperatures = slots.map(slot => Number(slot.dataset.temp));

    const width = container.clientWidth;
    const height = svg.clientHeight || 120;

    svg.setAttribute("viewBox", `0 0 ${width} ${height}`);

    // Add some padding so small temperature differences aren't exaggerated too much.
    const minTemp = Math.min(...temperatures);
    const maxTemp = Math.max(...temperatures);
    const tempRange = Math.max(maxTemp - minTemp, 1);

    const chartTop = 40;
    const chartBottom = height - 20;
    const slotWidth = width / slots.length;

    const points = slots.map((slot, i) => {
        const x = Math.round(slotWidth * i + slotWidth / 2);

        const y = Math.round(
            chartTop +
            ((maxTemp - temperatures[i]) / tempRange) * (chartBottom - chartTop));

        return {x, y, temperature: temperatures[i]};
    });

    const path = smoothPath(points);
    const fillPath = `
        ${path}
        L ${points.at(-1).x} ${height}
        L ${points[0].x} ${height}
        Z
    `;

    svg.innerHTML = `
        <defs>
            <linearGradient
                id="temperature-fill"
                x1="0"
                y1="0"
                x2="0"
                y2="1"
            >
                <stop offset="0%" stop-color="#333" />
                <stop offset="100%" stop-color="#000" />
            </linearGradient>
        </defs>

        <path
            d="${fillPath}"
            fill="url(#temperature-fill)"
            stroke="none"
        />

        <path
            d="${path}"
            fill="none"
            stroke="#fff"
            stroke-width="3"
            stroke-linecap="round"
            stroke-linejoin="round"
        />

        ${points.map(point => `
            <circle
                cx="${point.x}"
                cy="${point.y}"
                r="6"
                fill="#fff"
                stroke="none"
                stroke-width="6"
            />

            <text
                x="${point.x}"
                y="${point.y - 20}"
                text-anchor="middle"
                font-family="Inter, sans-serif"
                font-size="25px"
                font-weight="400"
                fill="#fff"
            >
                ${Math.round(point.temperature)}°
            </text>
        `).join("")}
    `;
}