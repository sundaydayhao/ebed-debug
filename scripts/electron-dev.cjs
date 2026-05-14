const { spawn } = require("node:child_process");
const path = require("node:path");

process.env.ELECTRON_MIRROR ||= "https://npmmirror.com/mirrors/electron/";

const electronPath = require("electron");
const appRoot = path.join(__dirname, "..");
const child = spawn(electronPath, ["--no-sandbox", "."], {
  cwd: appRoot,
  env: process.env,
  stdio: "inherit"
});

for (const signal of ["SIGINT", "SIGTERM"]) {
  process.on(signal, () => {
    if (!child.killed) child.kill(signal);
  });
}

child.on("error", (error) => {
  console.error(error);
  process.exit(1);
});

child.on("exit", (code, signal) => {
  if (typeof code === "number") {
    process.exit(code);
  }

  const signalExitCodes = {
    SIGINT: 130,
    SIGTERM: 143
  };
  process.exit(signalExitCodes[signal] || 1);
});
