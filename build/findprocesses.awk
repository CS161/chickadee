BEGIN {
    optional = 2;
    MIN = MIN + 0;
}
/^# *if  *SOL/ {
    skip = 0;
    if (match($0, "^# *if  *SOL *>= *")) {
        skip = SOL < (substr($0, RLENGTH) + 0);
    } else if (match($0, "^# *if  *SOL *> *")) {
        skip = SOL <= (substr($0, RLENGTH) + 0);
    } else if (match($0, "^# *if  *SOL *== *")) {
        skip = SOL != (substr($0, RLENGTH) + 0);
    } else if (match($0, "^# *if  *SOL *!= *")) {
        skip = SOL == (substr($0, RLENGTH) + 0);
    } else if (match($0, "^# *if  *SOL *<= *")) {
        skip = SOL > (substr($0, RLENGTH) + 0);
    } else if (match($0, "^# *if  *SOL *< *")) {
        skip = SOL >= (substr($0, RLENGTH) + 0);
    }
    if (skip) {
        optional = 2;
        nextfile;
    }
    next;
}
/^# *if  *LAB/ {
    skip = 0;
    if (match($0, "^# *if  *LAB *>= *")) {
        skip = LAB < (substr($0, RLENGTH) + 0);
    } else if (match($0, "^# *if  *LAB *> *")) {
        skip = LAB <= (substr($0, RLENGTH) + 0);
    } else if (match($0, "^# *if  *LAB *== *")) {
        skip = LAB != (substr($0, RLENGTH) + 0);
    } else if (match($0, "^# *if  *LAB *!= *")) {
        skip = LAB == (substr($0, RLENGTH) + 0);
    } else if (match($0, "^# *if  *LAB *<= *")) {
        skip = LAB > (substr($0, RLENGTH) + 0);
    } else if (match($0, "^# *if  *LAB *< *")) {
        skip = LAB >= (substr($0, RLENGTH) + 0);
    }
    if (skip) {
        optional = 2;
        nextfile;
    }
    next;
}
/^# *define  *CHICKADEE_OPTIONAL_PROCESS/ {
    match($0, "^# *define  *CHICKADEE_OPTIONAL_PROCESS[ \t\r\n]*");
    if (substr($0, RLENGTH) == "") {
        optional = 1;
    } else {
        optional = substr($0, RLENGTH) + 0;
    }
    next;
}
{
    f = FILENAME;
    sub("\\.cc$", "", f);
    if (optional == 0 || (MIN < 0 && DISK) || (MIN == 0 && optional == 2) || ("p-" CHICKADEE_FIRST_PROCESS) == f) {
        print f;
    }
    optional = 2;
    nextfile;
}
