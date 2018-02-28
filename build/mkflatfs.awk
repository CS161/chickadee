BEGIN {
    OFS = ""; files = ""
}
{
    for (i = 1; i <= NF; ++i) {
        name = $i;
        gsub(/^obj\//, "", name);
        gsub(/^p-/, "", name);
        prefix = "_binary_" $i;
        gsub(/[^a-zA-Z0-9_]/, "_", prefix);
        print "extern const unsigned char ", prefix, "_start[];";
        print "extern const unsigned char ", prefix, "_end[];";
        files = files "    { \"" name "\", " prefix "_start, " prefix "_end },\n";
    }
}
END {
    print "static const flatfs_file flatfs_files[] = {";
    print files, "};";
}
