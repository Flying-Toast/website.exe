#!/usr/bin/awk -f

BEGIN {
	ORS = "";
	arg = ARGV[1];
	sub(/tmpl\//, "", arg);
	sub(/\./, "_", arg);
	funcname = "_TMPLFUNC_" arg;
	funcargidx = 0;
	stringified = "";
}

function addstr(part) {
	stringified = stringified part;
}

1 {
	gsub(/$/, "\\n", $0);
	gsub(/"/, "\\\"", $0);
	patsplit($0, substs, /<%= %[a-z]+ [a-z_]+ %>/, constparts);

	addstr(constparts[0]);
	for (k in substs) {
		match(substs[k], /<%= %[a-z]+/);
		# 4 = len("<%= ")
		fspec = substr(substs[k], RSTART + 4, RLENGTH - 4);

		match(substs[k], /[a-z_]+ %>/);
		# 3 = len(" %>")
		argname = substr(substs[k], RSTART, RLENGTH - 3);

		if (fmtspecof[argname] && fmtspecof[argname] != fspec) {
			printf( \
				"TEMPLATE ERROR(%s): `%s` has conflicting format specifiers: %s and %s\n", \
				ARGV[1], argname, fmtspecof[argname], fspec \
			) > "/dev/stderr";
			exit 1;
		}
		fmtspecof[argname] = fspec;
		addstr(fspec);
		addstr(constparts[k]);

		funcargs[funcargidx++] = argname;
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
		} else {
			printf("TEMPLATE ERROR(%s): I don't know the format spec %s", ARGV[1], spec) > "/dev/stderr";
			exit 1;
		}
		print(argname ";\n");
	}
	print("};\n");

	print("static void " funcname "(int fd, struct _tmplargs_" arg " args)\n");
	print("{\n");
	print("\tdprintf(\n");
	print("\t\tfd,\n");
	print("\t\t\"" stringified "\",\n")
	for (k in funcargs) { lastfuncargidx = k; }
	for (k in funcargs) {
		print("\t\targs." funcargs[k]);
		if (k != lastfuncargidx) {
			print(",");
		}
		print("\n");
	}
	print("\t);\n");
	print("}\n");
}
