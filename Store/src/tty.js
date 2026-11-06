// Loaded into nwlink's process with -r: nwlink draws its upload progress bar (cli-progress)
// only on a terminal, and it runs without one under Numstore. Pretending stderr is a
// terminal makes it print "Upload [...] 42% | ETA: 30s", which installer.js reads.
for (const s of [process.stdout, process.stderr]) {
  s.isTTY = true;
  s.columns = 100;
  s.rows = 24;
}
