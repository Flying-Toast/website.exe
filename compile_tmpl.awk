BEGIN {
	ORS = "";
	arg = ARGV[1];
	sub(/^tmpl\//, "", arg);
	gsub(/\./, "_", arg);
	funcname = "_tmplfunc_" arg;
	funcargidx = 0;
	stringified = "";
}

function addstr(part) {
	stringified = stringified part;
}

function tmplsplit(str, substs, pat, constparts) {
	for (k in substs) { delete substs[k]; }
	for (k in constparts) { delete constparts[k]; }

	cpidx = 0;
	ssidx = 1;
	while (match(str, pat)) {
		constparts[cpidx++] = substr(str, 1, RSTART - 1);
		substs[ssidx++] = substr(str, RSTART, RLENGTH);
		str = substr(str, RSTART + RLENGTH);
	}
	constparts[cpidx] = str;

	for (k in constparts) {
		gsub("%", "%%", constparts[k]);
	}
}

1 {
	gsub(/\\/, "\\\\");
	gsub(/$/, "\\n", $0);
	gsub(/\t/, "\\t", $0);
	gsub(/"/, "\\\"", $0);
	# avoid pesky trigraphs:
	gsub(/\?/, "\"\"?\"\"");

	tmplsplit($0, substs, "<%= %[a-z]+ [a-z_]+ %>", constparts);

	addstr(constparts[0]);
	substslen = 0;
	for (k in substs) { substslen++; }
	for (i = 1; i <= substslen; i++) {
		match(substs[i], /<%= %[a-z]+/);
		# 4 = len("<%= ")
		fspec = substr(substs[i], RSTART + 4, RLENGTH - 4);

		match(substs[i], /[a-z_]+ %>/);
		# 3 = len(" %>")
		argname = substr(substs[i], RSTART, RLENGTH - 3);

		if (fmtspecof[argname] && fmtspecof[argname] != fspec) {
			printf( \
				"TEMPLATE ERROR(%s): `%s` has conflicting format specifiers: %s and %s\n", \
				ARGV[1], argname, fmtspecof[argname], fspec \
			) > "/dev/stderr";
			exit 1;
		}
		fmtspecof[argname] = fspec;
		addstr(fspec);
		addstr(constparts[i]);

		funcargs[++funcargidx] = argname;
	}
}

END {
	print("struct _tmplargs_" arg " {\n");

	for (argname in fmtspecof) {
		spec = fmtspecof[argname]
		print("\t");
		if (spec == "%s") {
			print("char *");
		} else if (spec == "%lu") {
			print("unsigned long ");
		} else if (spec == "%d") {
			print("int ");
		} else if (spec == "%hu") {
			print("unsigned short ");
		} else {
			printf("TEMPLATE ERROR(%s): I don't know the format spec %s\n", ARGV[1], spec) > "/dev/stderr";
			exit 1;
		}
		print(argname ";\n");
	}
	print("};\n");

	print("static void " funcname "(int fd, struct _tmplargs_" arg " *args)\n");
	print("{\n");
	print("\tdprintf(\n");
	print("\t\tfd,\n");
	print("\t\t\"" stringified "\"")
	for (k in funcargs) { funcargslen++; }
	if (funcargslen) {
		print(",");
	}
	print("\n");
	for (i = 1; i <= funcargslen; i++) {
		print("\t\targs->" funcargs[i]);
		if (i != funcargslen) {
			print(",");
		}
		print("\n");
	}
	print("\t);\n");
	print("}\n");
}
