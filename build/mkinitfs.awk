BEGIN {
    OFS = ""; files = ""
}
{
    for (i = 1; i <= NF; ++i) {
        name = $i;
        gsub(/^\.\//, "", name);
        gsub(/^initfs\//, "", name);
        gsub(/^obj\/p-/, "", name);
        gsub(/^obj\//, "", name);
        prefix = "_binary_" $i;
        gsub(/[^a-zA-Z0-9_]/, "_", prefix);
        print "extern unsigned char ", prefix, "_start[];";
        print "extern unsigned char ", prefix, "_end[];";
        files = files "    memfile(\"" name "\", " prefix "_start, " prefix "_end),\n";
    }
}
END {
    print "memfile memfile::initfs[initfs_size] = {";
    print files, "};";
}
