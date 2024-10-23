function load() {    
    const queryButton = document.getElementById("queryButton");
    queryButton.addEventListener("click", onQueryClick);

    const yesterday = new Date();
    yesterday.setTime(yesterday.getTime() - 6 * 60 * 60 * 1000)
    
    const startTime = document.getElementById("startTime");
    setDate(startTime, yesterday);

    const endTime = document.getElementById("endTime");
    setDate(endTime, new Date());
}

function setDate(input, date) {
    date.setMinutes(date.getMinutes() - date.getTimezoneOffset());
    input.value = date.toISOString().slice(0, 16);
}

async function onQueryClick() {
    const queryInput = document.getElementById("queryInput");
    const query      = queryInput.value;

    const startTime = document.getElementById("startTime").value;
    const endTime   = document.getElementById("endTime").value;    
    
    const response  = await fetch(`/api/query?query=${query}&start=${startTime}&end=${endTime}`);
    const body      = await response.arrayBuffer();
    const binsView  = new Uint8Array(body, 0, 4);
    
    const bins      = binsView[0] + (binsView[1] << 8) + (binsView[2] << 16) + (binsView[3] << 24);
    const histogram = new Int32Array(body, 4, bins);
    const logs      = new TextDecoder().decode(new Uint8Array(body, (bins + 1) * 4));
    
    document.body.replaceChildren(document.body.children[0]);
    drawGraph(histogram);

    const results = document.createElement("div");
    results.setAttribute("id", "results");
    for (const line of logs.split("\n")) {
	if (line.length > 0) {
	    const result     = document.createElement("p");
	    result.innerHTML = line.replaceAll(query, `<span class=\"found\">${query}</span>`);
	    results.appendChild(result);
	}
    }
    document.body.appendChild(results);    
}

function drawGraph(values) {
    const graphWidth        = 600;
    const graphHeight       = 400;
    const animationDuration = "0.25s";
    
    const graph = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    graph.setAttribute("width", graphWidth);
    graph.setAttribute("height", graphHeight);

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

    let   barWidth = graphWidth / values.length;
    const padding  = 0.1 * barWidth;
    barWidth       = barWidth - padding - padding / values.length;

    let x = padding;
    for (const value of values) {
	const barHeight = (value - minValue) / (maxValue - minValue) * (graphHeight - padding);
	
	const bar = document.createElementNS("http://www.w3.org/2000/svg", "rect");
	bar.setAttribute("x", x);
	bar.setAttribute("width", barWidth);
	bar.setAttribute("fill", "#F5FF90");

	const splines = "0 0.5 0 0.5; 0 0.5 0 0.5";

	const yAnimation = document.createElementNS("http://www.w3.org/2000/svg", "animate");
	yAnimation.setAttribute("attributeName", "y");
	yAnimation.setAttribute("from", graphHeight);
	yAnimation.setAttribute("to", graphHeight - barHeight);
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

	x += barWidth + padding;
    }
    
    document.body.appendChild(graph);
}

window.addEventListener("load", load);
