// Puts the window together. Each screen lives in a file of its own; this one only
// builds them and connects them.

import "./styles.css";
import { appInfo } from "./api";
import { el, append } from "./dom";

async function start(): Promise<void> {
  const info = await appInfo();
  append(document.body, el("main", { className: "placeholder" }, `${info.product} ${info.displayVersion}`));
}

void start();
