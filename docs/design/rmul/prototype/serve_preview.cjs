/** 仅向本机提供原型和指定字体；无配置写入、设备访问或客户端网络操作。Ctrl+C 退出。 */
const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const routes = new Map([
    ["/", [path.join(__dirname, "index.html"), "text/html; charset=utf-8"]],
    ["/resources/fonts/noto_sans_sc.ttf", [path.resolve(__dirname, "../../../../resources/fonts/noto_sans_sc.ttf"), "font/ttf"]],
]);
const server = http.createServer((request, response) => {
    const route = routes.get(new URL(request.url, "http://localhost").pathname);
    if (!route) { response.writeHead(404); response.end(); return; }
    response.writeHead(200, { "Content-Type": route[1], "Cache-Control": "no-store" });
    fs.createReadStream(route[0]).pipe(response);
});
server.listen(0, "127.0.0.1", () => console.log(`http://127.0.0.1:${server.address().port}/?view=session`));
