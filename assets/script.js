function load() {    
    const queryButton = document.getElementById("queryButton");
    queryButton.addEventListener("click", onQueryClick);

    const yesterday = new Date();
    yesterday.setTime(yesterday.getTime() - 6 * 60 * 60 * 1000)
    
    const startTime = document.getElementById("startTime");
    setDate(startTime, yesterday);

    const endTime = document.getElementById("endTime");
    setDate(endTime, new Date());

    onQueryClick();
}

function setDate(input, date) {
    date.setMinutes(date.getMinutes() - date.getTimezoneOffset());
    input.value = date.toISOString().slice(0, 16);
}

async function onQueryClick() {
    const query     = document.getElementById("queryInput").value;
    const startTime = document.getElementById("startTime").value;
    const endTime   = document.getElementById("endTime").value;

    /*
    const query     = "INFO";
    const startTime = "2024-10-08T00:00";
    const endTime   = "2024-10-10T00:00";
    */

    const response  = await fetch(`/api/query?query=${query}&start=${startTime}&end=${endTime}`);
    const body      = await response.arrayBuffer();
    const binsView  = new Uint8Array(body, 0, 4);
    
    const bins      = binsView[0] + (binsView[1] << 8) + (binsView[2] << 16) + (binsView[3] << 24);
    const histogram = new Int32Array(body, 4, bins);
    const logs      = new TextDecoder().decode(new Uint8Array(body, (bins + 1) * 4));

    document.body.replaceChildren(document.body.children[0]);
    drawGraph(logs.length == 0 ? null : histogram);

    const results = document.createElement("div");
    results.setAttribute("id", "results");
    for (let line of logs.split("\n")) {
	if (line.length > 0) {
	    for (const word of query.split(' ')) {
		line = line.replaceAll(word, `<span class=\"found\">${word}</span>`);
	    }
	    const result     = document.createElement("p");
	    result.innerHTML = line;
	    results.appendChild(result);
	}
    }
    document.body.appendChild(results);    
}

function drawGraph(values) {
    const graphWidth        = 600;
    const graphHeight       = 400;
    const graphPadding      = 25;
    const animationDuration = "0.25s";
    
    const graph = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    graph.setAttribute("width", graphWidth);
    graph.setAttribute("height", graphHeight);
    document.body.appendChild(graph);

    if (values == null) {
	const text       = document.createElementNS("http://www.w3.org/2000/svg", "text");
	text.textContent = "No matches found.";
	text.setAttribute("text-anchor", "middle");
	text.setAttribute("font-size", "1.5em");
	text.setAttribute("x", "50%");
	text.setAttribute("y", "50%");
	graph.appendChild(text);
	return;
    }

    let maxValue = values[0];
    let minValue = values[0];
    for (const value of values) {
	if (value > maxValue) {
	    maxValue = value;
	}
	if (value < minValue) {
	    minValue = value;
	}
    }

    const xAxis = document.createElementNS("http://www.w3.org/2000/svg", "line");
    xAxis.setAttribute("x1", graphPadding);
    xAxis.setAttribute("y1", graphHeight - graphPadding);
    xAxis.setAttribute("x2", graphWidth - graphPadding);
    xAxis.setAttribute("y2", graphHeight - graphPadding);
    xAxis.setAttribute("stroke", "#4a5462");
    graph.appendChild(xAxis);

    const yAxis = document.createElementNS("http://www.w3.org/2000/svg", "line");
    yAxis.setAttribute("x1", graphPadding);
    yAxis.setAttribute("y1", graphPadding);
    yAxis.setAttribute("x2", graphPadding);
    yAxis.setAttribute("y2", graphHeight - graphPadding);
    yAxis.setAttribute("stroke", "#4a5462");
    graph.appendChild(yAxis);

    const subGraphWidth  = graphWidth  - 2 * graphPadding;
    const subGraphHeight = graphHeight - 2 * graphPadding;

    let   barWidth = subGraphWidth / values.length;
    const padding  = 0.1 * barWidth;
    barWidth       = barWidth - padding - padding / values.length;

    let x = graphPadding + padding;
    for (const value of values) {
	const barHeight = (value - minValue) / (maxValue - minValue) * (subGraphHeight - padding);

	if (value > 0) {
	    const bar = document.createElementNS("http://www.w3.org/2000/svg", "rect");
	    bar.setAttribute("x", x);
	    bar.setAttribute("width", barWidth);
	    bar.setAttribute("fill", "#f9a31b");

	    const splines = "0 0.5 0 0.5; 0 0.5 0 0.5";

	    const yAnimation = document.createElementNS("http://www.w3.org/2000/svg", "animate");
	    yAnimation.setAttribute("attributeName", "y");
	    yAnimation.setAttribute("from", graphHeight - graphPadding);
	    yAnimation.setAttribute("to", graphHeight - graphPadding - barHeight);
	    yAnimation.setAttribute("dur", animationDuration);
	    yAnimation.setAttribute("fill", "freeze");
	    
	    const heightAnimation = document.createElementNS("http://www.w3.org/2000/svg", "animate");
	    heightAnimation.setAttribute("attributeName", "height");
	    heightAnimation.setAttribute("from", 0);
	    heightAnimation.setAttribute("to", barHeight);
	    heightAnimation.setAttribute("dur", animationDuration);
	    heightAnimation.setAttribute("fill", "freeze");

	    bar.appendChild(yAnimation);
	    bar.appendChild(heightAnimation);
	    graph.appendChild(bar);
	}

	x += barWidth + padding;
    }
}

window.addEventListener("load", load);
