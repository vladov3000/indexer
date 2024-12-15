import { apiKey } from "/api_key.js";

// Base off of: https://github.com/groq/groq-api-cookbook/blob/main/tutorials/function-calling-sql/json-mode-function-calling-for-sql.ipynb
const systemPrompt = `
You are Groq Advisor, and you are tasked with answering a user's questions about their application logs.

You can search application logs by prefixing a keyword with RUN_SEARCH.

The resulting logs will start with SEARCH_RESULT.

Respond with only a search you want to preform or your final answer.

Question:
--------
{user_question}
--------
`;

let page = 0;

async function load() {
    const results            = document.getElementById("mainResults");
    results.style.visibility = "hidden";
    results.addEventListener("keydown", e => {
	if (e.key === "q") {
	    const queryInput = document.getElementById("queryInput");
	    queryInput.value = window.getSelection().toString();

	    // We need to delay this to the next run of the event loop,
	    // because otherwise "mainResults" will be focused instead of
	    // "queryInput".
	    setTimeout(() => queryInput.focus(), 0);
	}
    });
    
    const queryButton = document.getElementById("queryButton");
    queryButton.addEventListener("click", onQueryClick);

    const yesterday = new Date();
    yesterday.setTime(yesterday.getTime() - 62 * 24 * 60 * 60 * 1000)
    
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

    // const query     = "ASK What happened with request f457dd4c-207b-4db3-8c52-afbfd97e636f?";
    
    /*
    const query     = "INFO";
    const startTime = "2024-10-09T00:00";
    const endTime   = "2024-10-11T00:00";
    */

    if (query.startsWith("ASK ")) {
        let question = query.slice("ASK ".length);

	    const thinking = document.getElementById("thinking");
	    thinking.replaceChildren();

        for (let i = 0; i < 3; i++) {
            const message = await ask(question);
            // const message = "RUN_SEARCH requestId=cfc3a428-9bc3-42b4-878f-cf9aa5ab95cc";

            const thinkingText       = document.createElement("p");
            thinkingText.textContent = message;
            thinkingText.classList.add("thoughtBubble");
            thinking.appendChild(thinkingText);

            if (message.startsWith("RUN_SEARCH ")) {
                const newQuery = message.slice("RUN_SEARCH ".length);
                const logs = await getLogs(newQuery, startTime, endTime);

                const results = document.createElement("div");
                results.classList.add("results");
                thinking.append(results);

                drawLogs(newQuery, logs, results);

                question += '\n' + message;
                question += '\n' + "SEARCH_RESULT" + logs;
            } else {
                break;
            }
        }

        return;
	
        /*
        const thinkingText       = document.createElement("p");
        thinkingText.textContent = "RUN_QUERY requestId=cfc3a428-9bc3-42b4-878f-cf9aa5ab95cc";
        thinkingText.classList.add("thoughtBubble");
        thinking.appendChild(thinkingText);

        setTimeout(() => {
            const results = document.createElement("div");
            results.classList.add("results");
            thinking.appendChild(results);
            
            drawLogs("requestId=cfc3a428-9bc3-42b4-878f-cf9aa5ab95cc", `2024/10/21 19:46:50 ERROR Failed to insert user with unique username requestId=cfc3a428-9bc3-42b4-878f-cf9aa5ab95cc error="ERROR: duplicate key value violates unique constraint \"users_username_key\" (SQLSTATE 23505)"`, results);
        }, 500);

        setTimeout(() => {
            const thinkingText2       = document.createElement("p");
            thinkingText2.textContent = `Based on the provided log entries, the "signUp" request with requestId "f457dd4c-207b-4db3-8c52-afbfd97e636f" was processed, but there was an error inserting the user into the database due to a duplicate key value violating the unique constraint "users\_username\_key" (SQLSTATE 23505). This means that the username of the user is not unique, and it is already present in the database.`;
            thinkingText2.classList.add("thoughtBubble");
            thinking.appendChild(thinkingText2);
        }, 1000);
        
        return;
        */
    }

    const parameters = `query=${query}&start=${startTime}&end=${endTime}&page=${page}`;
    const response   = await fetch(`api/query?${parameters}`);

    for await (const chunk of response.body) {
	const reader = { input: chunk, offset: 0 };

	while (reader.offset < reader.input.length) {
	    const tag = read_int(reader);

	    if (tag === 1) {
		const logs_size = read_int(reader);
		const logs      = read_string(reader, logs_size);
		noResults       = logs.length == 0;
		
		const results            = document.getElementById("mainResults");
		results.style.visibility = "visible";
		results.replaceChildren();

		drawLogs(query, logs, results);

	    } else if (tag === 2) {
		const bins      = read_int(reader);
		const histogram = read_ints(reader, bins);
		drawGraph(histogram);
	    }
	}
    }
}

async function ask(question) {
    const response = await fetch("https://api.groq.com/openai/v1/chat/completions", {
        method: "POST",
        headers: {
            "Content-Type": "application/json",
            "Authorization": `Bearer ${apiKey}`,
        },
        body: JSON.stringify({
            "model": "llama3-8b-8192",
            "messages": [
                {"role": "system", "content": systemPrompt },
                {"role": "user", "content": question },
            ],
            "temperature": 0.7
        }),
    });

    const responseJson = await response.json();
    return responseJson.choices[0].message.content;
}

async function getLogs(query, startTime, endTime) {
    const parameters = `query=${query}&start=${startTime}&end=${endTime}&page=${page}`;
    const response   = await fetch(`api/query?${parameters}`);
    const body       = response.body.getReader();

    while (true) {
	const { done, value: chunk } = await body.read();
	if (done) {
	    break;
	}
	
        const reader = { input: chunk, offset: 0 };
        const tag    = read_int(reader);
        if (tag === 1) {
	    const logs_size = read_int(reader);
	    const logs      = read_string(reader, logs_size);
	    return logs;
        }
    }
    
    return "";
}

function read_int(reader) {
    const input  = reader.input;
    const offset = reader.offset;
    
    let result = 0;
    result += input[offset + 3] << 24;
    result += input[offset + 2] << 16;
    result += input[offset + 1] <<  8;
    result += input[offset];
    
    reader.offset += 4;
    return result;
}

function read_ints(reader, count) {
    const input    = reader.input;
    const offset   = reader.offset;
    const result   = new Int32Array(input.buffer.slice(input.byteOffset), offset, count);
    reader.offset += 4 * count;
    return result;
}

function read_string(reader, size) {
    const input    = reader.input;
    const offset   = reader.offset;
    const result   = new TextDecoder().decode(input.slice(offset, offset + size))
    reader.offset += size;
    return result;
}

function drawGraph(values) {
    const graphWidth        = 600;
    const graphHeight       = 400;
    const graphPadding      = 25;
    const animationDuration = "0.25s";

    const container = document.getElementById("graph");
    
    const graph = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    graph.setAttribute("width", graphWidth);
    graph.setAttribute("height", graphHeight);
    container.replaceChildren(graph);

    let allZero = true;
    for (const value of values) {
	if (value > 0) {
	    allZero = false;
	    break;
	}
    }
    
    if (allZero) {
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
	    bar.setAttribute("y", graphHeight - graphPadding - barHeight);
	    bar.setAttribute("width", barWidth);
	    bar.setAttribute("height", barHeight);
	    bar.setAttribute("fill", "#f9a31b");

	    /*
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
	    */
	    graph.appendChild(bar);
	}

	x += barWidth + padding;
    }
}

function drawLogs(query, logs, results) {
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
