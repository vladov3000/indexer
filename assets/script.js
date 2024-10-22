const logs = `
2024/10/21 19:46:41 INFO Listening port=8080
2024/10/21 19:46:49 INFO New signUp request requestId=6961230c-bc7c-4636-a88f-f1f42a9a631d
2024/10/21 19:46:49 ERROR Password mismatch requestId=6961230c-bc7c-4636-a88f-f1f42a9a631d error="crypto/bcrypt: hashedPassword is not the hash of the given password"
2024/10/21 19:46:50 INFO New signUp request requestId=cfc3a428-9bc3-42b4-878f-cf9aa5ab95cc
2024/10/21 19:46:50 ERROR Failed to insert user with unique username requestId=cfc3a428-9bc3-42b4-878f-cf9aa5ab95cc error="ERROR: duplicate key value violates unique constraint \"users_username_key\" (SQLSTATE 23505)"
2024/10/21 19:46:52 INFO New signUp request requestId=50c9655a-1a50-4480-84df-02d098531620
2024/10/21 19:46:52 ERROR Password mismatch requestId=50c9655a-1a50-4480-84df-02d098531620 error="crypto/bcrypt: hashedPassword is not the hash of the given password"
2024/10/21 19:46:53 INFO New signUp request requestId=f457dd4c-207b-4db3-8c52-afbfd97e636f
2024/10/21 19:46:53 ERROR Failed to insert user with unique username requestId=f457dd4c-207b-4db3-8c52-afbfd97e636f error="ERROR: duplicate key value violates unique constraint \"users_username_key\" (SQLSTATE 23505)"
2024/10/21 19:46:54 INFO New signUp request requestId=a6977bb0-feb9-46f5-b301-3ad8041a7b5d
2024/10/21 19:46:54 ERROR Password mismatch requestId=a6977bb0-feb9-46f5-b301-3ad8041a7b5d error="crypto/bcrypt: hashedPassword is not the hash of the given password"
2024/10/21 19:46:59 INFO New signUp request requestId=f01dcfee-1d3e-4fba-b9f1-ace395a7791b
2024/10/21 19:47:18 INFO New signUp request requestId=a825a67c-8e7d-428d-bbd0-570c69ea6343
2024/10/21 19:47:18 ERROR Failed to find user requestId=a825a67c-8e7d-428d-bbd0-570c69ea6343
2024/10/21 19:47:19 INFO New signUp request requestId=702d3c59-6166-451f-abd6-d93e02bd72e6
2024/10/21 19:47:19 ERROR Failed to find user requestId=702d3c59-6166-451f-abd6-d93e02bd72e6
2024/10/21 19:47:20 INFO New signUp request requestId=0cd5b6c2-3f6d-4dcc-a163-4e19b817a73a
2024/10/21 19:47:20 ERROR Failed to find user requestId=0cd5b6c2-3f6d-4dcc-a163-4e19b817a73a
`;

function load() {
    const socket = new WebSocket("/websocket");
    
    const search = document.getElementById("queryButton");
    search.addEventListener("click", onSearchClick);

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

function onSearchClick() {
    document.body.replaceChildren(document.body.children[0]);

    const startTime = document.getElementById("startTime");
    const start     = new Date(startTime.value).getTime();

    const endTime = document.getElementById("endTime");
    const end     = new Date(endTime.value).getTime();

    const queryInput = document.getElementById("queryInput");
    const query      = queryInput.value;
    const buckets    = new Array(100).fill(0);

    const lines = logs.split("\n");
    for (const line of lines) {
	if (line.includes(query) && line.length > 0) {
	    const [day, time]               = line.split(" ").slice(0, 2);
	    const [hours, minutes, seconds] = time.split(":");
	    const date                      = new Date(day);
	    date.setHours(hours, minutes, seconds)
	    const timestamp = date.getTime();
	    if (start <= timestamp && timestamp <= end) {
		const index = Math.floor((timestamp - start) / (end - start) * buckets.length);
		buckets[index]++;
	    }
	}
    }
    
    drawGraph(buckets);

    const results = document.createElement("div");
    results.setAttribute("id", "results");
    for (const line of lines) {
	if (line.includes(query) && line.length > 0) {
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

    const paddingFactor = 0.10;
    const barWidth      = graphWidth / (values.length + paddingFactor);
    const padding       = barWidth * paddingFactor

    let x = 0;
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

	x += padding + barWidth;
    }
    
    document.body.appendChild(graph);
}

window.addEventListener("load", load);
