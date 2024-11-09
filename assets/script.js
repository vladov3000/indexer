let page = 0;

function load() {    
    const queryButton = document.getElementById("queryButton");
    queryButton.addEventListener("click", onQueryClick);

    const yesterday = new Date();
    yesterday.setTime(yesterday.getTime() - 6 * 60 * 60 * 1000)
    
    const startTime = document.getElementById("startTime");
    setDate(startTime, yesterday);

    const endTime = document.getElementById("endTime");
    setDate(endTime, new Date());

    // onQueryClick();
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
    const query     = "ASK What happened with request f457dd4c-207b-4db3-8c52-afbfd97e636f?";
    const startTime = "2024-10-09T00:00";
    const endTime   = "2024-10-11T00:00";
    */

    if (query.startsWith("ASK ")) {
	document.body.replaceChildren(document.body.children[0]);
	
	const thinkingText       = document.createElement("p");
	thinkingText.textContent = "RUN_QUERY requestId=f457dd4c-207b-4db3-8c52-afbfd97e636f";
	thinkingText.classList.add("thinking");
	document.body.appendChild(thinkingText);

	setTimeout(() => {
	    drawLogs("requestId=f457dd4c-207b-4db3-8c52-afbfd97e636f", `2024/10/21 19:46:53 INFO New signUp request requestId=f457dd4c-207b-4db3-8c52-afbfd97e636f\n2024/10/21 19:46:53 ERROR Failed to insert user with unique username requestId=f457dd4c-207b-4db3-8c52-afbfd97e636f error="ERROR: duplicate key value violates unique constraint \"users_username_key\" (SQLSTATE 23505)"`);
	}, 1000);

	setTimeout(() => {
	    const thinkingText2       = document.createElement("p");
	    thinkingText2.textContent = `Based on the provided log entries, the "signUp" request with requestId "f457dd4c-207b-4db3-8c52-afbfd97e636f" was processed, but there was an error inserting the user into the database due to a duplicate key value violating the unique constraint "users\_username\_key" (SQLSTATE 23505). This means that the username of the user is not unique, and it is already present in the database.`;
	    thinkingText2.classList.add("thinking");
	    document.body.appendChild(thinkingText2);
	}, 2000);
	
	return;
    }
    
    const parameters = `query=${query}&start=${startTime}&end=${endTime}&page=${page}`;
    const response   = await fetch(`/api/query?${parameters}`);
    const body       = await response.arrayBuffer();
    let   offset     = 0;;
    
    const bins = new Int32Array(body, offset, 1)[0];
    offset += 4;
    
    const histogram = new Int32Array(body, offset, bins);
    offset += histogram.byteLength;
    
    const logs = new TextDecoder().decode(new Uint8Array(body, offset));

    document.body.replaceChildren(document.body.children[0]);
    drawGraph(logs.length == 0 ? null : histogram);
    
    drawLogs(query, logs);
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

function drawLogs(query, logs) {
    const results = document.createElement("div");
    results.setAttribute("id", "results");
    document.body.appendChild(results);

    const header = document.createElement("div");
    header.setAttribute("id", "header");
    results.appendChild(header);

    const headerTitle       = document.createElement("h3");
    headerTitle.textContent = "Results";
    header.appendChild(headerTitle);

    const pageButtons = document.createElement("div");
    pageButtons.setAttribute("id", "pageButtons");
    header.appendChild(pageButtons);

    const previousPage       = document.createElement("button");
    previousPage.textContent = "Previous Page";
    previousPage.addEventListener("click", () => { page = page == 0 ? 0 : page - 1; onQueryClick(); });
    pageButtons.appendChild(previousPage);

    const pageText       = document.createElement("span");
    pageText.textContent = page;
    pageButtons.appendChild(pageText);

    const nextPage       = document.createElement("button");
    nextPage.textContent = "Next Page";
    nextPage.addEventListener("click", () => { page = page + 1; onQueryClick(); });
    pageButtons.appendChild(nextPage);

    for (let line of logs.split("\n")) {
	if (line.length > 0) {
	    for (const word of query.split(' ')) {
		if (word !== "OR") {
		    line = line.replaceAll(word, `<span class=\"found\">${word}</span>`);
		}
	    }
	    const result     = document.createElement("p");
	    result.innerHTML = line;
	    results.appendChild(result);
	}
    }

    if (logs.length == 0) {
	const result     = document.createElement("p");
	result.innerHTML = "No matches found.";
	results.appendChild(result);	
    }
}

window.addEventListener("load", load);
