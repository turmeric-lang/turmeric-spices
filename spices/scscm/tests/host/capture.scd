// capture.scd -- run a sclang program against a stand-in server address and
// write every message it sends, with its time, as a scsynth -N score.
//
//   sclang capture.scd <program.scd> <score.osc> <seconds>
//
// Server.default is marked running and its address replaced by an Event that
// records sendMsg/sendBundle, so SynthDef.add, Synth, Pbind.play and
// TempoClock-scheduled code all run as they would live, for <seconds> of real
// time. Bundles keep their latency. Stock SuperCollider only: the score's
// SynthDefs are the version this sclang writes.
var args = thisProcess.argv, prog = args[0], out = args[1], dur = args[2].asFloat;
var s = Server.default, log = List.new, t0, fake, clean;
clean = { |msg| msg.reject(_.isNil) };
fake = (
	addr: 2130706433, port: 57110, hostname: "127.0.0.1", ip: "127.0.0.1",
	isLocal: true,
	sendMsg: { |self ...msg| log.add([Main.elapsedTime - t0, clean.(msg)]) },
	sendBundle: { |self, time ...msgs|
		msgs.do { |m| log.add([Main.elapsedTime - t0 + (time ? 0), clean.(m)]) } },
	sendRaw: { |self ...a| },
	// Server:sync waits on its address. Nothing is asynchronous in the score
	// (scsynth -N applies each message in order), so return at once.
	sync: { |self ...a| nil },
	sendClumpedBundles: { |self, time ...msgs|
		msgs.do { |m| log.add([Main.elapsedTime - t0 + (time ? 0), clean.(m)]) } }
);
fake.know = true;
s.addr = fake;
s.statusWatcher.serverRunning = true;
t0 = Main.elapsedTime;
thisProcess.interpreter.executeFile(prog);
SystemClock.sched(dur, {
	// Time 0 is the first message, not the start of executeFile: compiling
	// the program takes a few ms that hclang's capture does not have.
	var start = if(log.isEmpty) { 0 } { log.collect(_[0]).minItem };
	var entries = log.asArray.collect { |e| [(e[0] - start).max(0), e[1]] };
	entries = entries.sort { |a, b| a[0] <= b[0] };
	Score(entries ++ [[dur, [\c_set, 0, 0]]]).writeOSCFile(out);
	("captured % messages".format(log.size)).postln;
	0.exit;
	nil
});
