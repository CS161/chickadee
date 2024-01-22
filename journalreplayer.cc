#if defined(CHICKADEE_KERNEL) || defined(CHICKADEE_PROCESS)
# include "lib.hh"
#else
# include <cstring>
# include <cinttypes>
# include <cassert>
# include <memory>
uint32_t crc32c(uint32_t crc, const void* buf, size_t len);
inline uint32_t crc32c(const void* buf, size_t len) {
    return crc32c(0, buf, len);
}
#endif
#include "cbyteswap.hh"
#include "chickadeefs.hh"

namespace chkfs {

// Constructor and destructor create an empty journalreplayer.

journalreplayer::journalreplayer()
    : jd_(nullptr), mr_(nullptr), ok_(true) {
}

journalreplayer::~journalreplayer() {
    delete[] mr_;
}


// journalreplayer::analyze(jd, nblocks)
//    Analyze the journal, which consists of `nblocks` blocks of data
//    loaded into memory at `jd`.

bool journalreplayer::analyze(unsigned char* jd, unsigned nblocks) {
    assert(!jd_);
    jd_ = jd;
    nb_ = nblocks;

    // analyze block contents
    mr_ = new (std::nothrow) metaref[nblocks];
    assert(mr_);
    nmr_ = 0;
    for (unsigned bi = 0; bi != nb_; ++bi) {
        analyze_block(bi);
    }
    if (!ok_) {
        return false;
    } else if (!nmr_) {
        message(-1U, "no metablocks found");
        return false;
    }

    // check metablock invariants
    // 1. No sequence number duplicates.
    // 2. `commit_boundary` monotonically increases.
    // 3. `complete_boundary` monotonically increases.
    // 4. `complete_boundary <= commit_boundary`.
    // 6. Completed tids are < `complete_boundary`.
    // 5. Committed tids are < `commit_boundary`.
    for (unsigned mi = 0; mi != nmr_; ++mi) {
        auto cur = mr_[mi].b;
        if (mi != 0) {
            auto last = mr_[mi - 1].b;
            if (cur->seq == last->seq) {
                error(mr_[mi].bi, "duplicate journal seqno");
                ok_ = false;
            }
            if (tid_lt(cur->commit_boundary, last->commit_boundary)) {
                error(mr_[mi].bi, "journal commit_boundary backtracked");
                ok_ = false;
            }
            if (tid_lt(cur->complete_boundary, last->complete_boundary)) {
                error(mr_[mi].bi, "journal complete_boundary backtracked");
                ok_ = false;
            }
        }
        if (tid_gt(cur->complete_boundary, cur->commit_boundary)) {
            error(mr_[mi].bi, "journal complete_boundary above commit_boundary");
            ok_ = false;
        }
        if (cur->nref
            && mi > 0
            && tid_lt(cur->tid, mr_[mi - 1].b->commit_boundary)) {
            error(mr_[mi].bi, "journal adds data to a committed transaction");
            ok_ = false;
        }
        if (cur->nref
            && tid_lt(cur->tid, mr_[mi].b->complete_boundary)) {
            error(mr_[mi].bi, "journal adds data to a completed transaction");
            ok_ = false;
        }
        if ((cur->flags & jf_complete)
            && tid_ge(cur->tid, cur->complete_boundary)) {
            error(mr_[mi].bi, "completed transaction above complete_boundary");
            ok_ = false;
        }
        if ((cur->flags & jf_commit)
            && tid_ge(cur->tid, cur->commit_boundary)) {
            error(mr_[mi].bi, "committed transaction above commit_boundary");
            ok_ = false;
        }
    }
    if (!ok_) {
        return false;
    }

    // Check transactions.
    // Every transaction in the region [complete_boundary, commit_boundary)
    // must be completely contained in the log, and have a commit record
    // but no complete record.
    // The last valid metablock has the relevant boundaries.
    tid_t complete_boundary = mr_[nmr_ - 1].b->complete_boundary;
    tid_t commit_boundary = mr_[nmr_ - 1].b->commit_boundary;
    message(-1U, "committable region is [%u,%u)", complete_boundary,
            commit_boundary);
    for (tid_t tid = complete_boundary; tid != commit_boundary; ++tid) {
        analyze_tid(tid);
    }

    // Mark all but the latest write to each data block with the
    // `jbf_overwritten` flag, indicating that previous writes
    // should be ignored.
    for (unsigned mx = nmr_; mx != 0; --mx) {
        auto jmb = mr_[mx - 1].b;
        if (tid_ge(jmb->tid, complete_boundary)
            && tid_lt(jmb->tid, commit_boundary)) {
            analyze_overwritten_blocks(mx);
        }
    }

    return ok_;
}


// journalreplayer::analyze() helper functions

bool journalreplayer::is_potential_metablock(const unsigned char* jd) {
    assert(!(reinterpret_cast<uintptr_t>(jd) & 0x7));
    auto jmb = reinterpret_cast<const jmetablock*>(jd);
    if (from_le(jmb->magic) != journalmagic) {
        return false;
    }
    auto checksum = from_le(jmb->checksum);
    return checksum == nochecksum
        || checksum == crc32c(jd + 16, blocksize - 16);
}

void journalreplayer::analyze_block(unsigned bi) {
    assert(bi < nb_);
    auto jd = jd_ + bi * blocksize;
    auto jmb = reinterpret_cast<jmetablock*>(jd);
    if (is_potential_metablock(jd)) {
        message(bi, "found potential metablock");
        jmb->seq = from_le(jmb->seq);
        jmb->tid = from_le(jmb->tid);
        jmb->commit_boundary = from_le(jmb->commit_boundary);
        jmb->complete_boundary = from_le(jmb->complete_boundary);
        jmb->flags = from_le(jmb->flags);
        jmb->nref = from_le(jmb->nref);

        // check flags
        if (jmb->flags & (jf_error | jf_corrupt)) {
            error(bi, "metablock marked jf_error (recoverable)");
            jmb->flags |= jf_error;
        }
        if (!(jmb->flags & jf_meta)) {
            error(bi, "metablock not marked with jf_meta (recoverable)");
            jmb->flags |= jf_error;
        }
        if (jmb->nref > ref_size) {
            error(bi, "metablock has too many refs (recoverable)");
            jmb->flags |= jf_error;
        }

        // check data checksums
        unsigned delta = 1;
        for (unsigned refi = 0; refi != jmb->nref; ++refi) {
            delta = analyze_block_reference(jmb, jmb->ref[refi], bi, delta);
        }

        // add non-erroneous metablocks to list in sequence number order
        if (!(jmb->flags & jf_error)) {
            unsigned x = 0;
            while (x != nmr_ && int32_t(jmb->seq - mr_[x].b->seq) >= 0) {
                ++x;
            }
            if (x != nmr_) {
                memmove(&mr_[x + 1], &mr_[x], (nmr_ - x) * sizeof(metaref));
            }
            mr_[x].bi = bi;
            mr_[x].b = jmb;
            ++nmr_;
        }
    } else if (from_le(jmb->magic) != 0) {
        if (from_le(jmb->magic) != journalmagic) {
            message(bi, "not a metablock (magic number %" PRIx64 ")",
                    from_le(jmb->magic));
        } else {
            message(bi, "not a metablock (checksum %x)",
                    from_le(jmb->checksum));
        }
    }
}

unsigned journalreplayer::analyze_block_reference(jmetablock* jmb,
        const jblockref& ref, unsigned bi, unsigned delta) {
    auto bflags = from_le(ref.bflags);
    auto bchecksum = from_le(ref.bchecksum);
    if (!(bflags & jbf_nonjournaled)) {
        if (delta >= nb_) {
            error(bi, "too many referenced datablocks");
            ok_ = false;
        }
        auto dbi = (bi + delta) % nb_;
        auto djd = jd_ + dbi * blocksize;
        if (is_potential_metablock(djd)) {
            error(dbi, "referenced datablock looks like metablock (recoverable)");
            jmb->flags |= jf_error;
        } else if (bchecksum != nochecksum
                   && bchecksum != crc32c(djd, blocksize)) {
            error(dbi, "referenced datablock has bad checksum (recoverable)");
            jmb->flags |= jf_error;
        }
        ++delta;
    }
    return delta;
}

void journalreplayer::analyze_tid(tid_t tid) {
    unsigned flags = 0;

    for (unsigned mi = 0; mi != nmr_; ++mi) {
        auto jmb = mr_[mi].b;
        unsigned tid_flags = 0;
        if (flags != 0) {
            auto want_seq = tid_t(mr_[mi - 1].b->seq + 1);
            if (jmb->seq == want_seq) { // no seq numbers missing
                if (tid_gt(jmb->commit_boundary, tid)) {
                    tid_flags |= jf_commit;
                }
                if (tid_gt(jmb->complete_boundary, tid)) {
                    tid_flags |= jf_complete;
                }
            } else if (!(flags & jf_commit)) {
                error(mr_[mi].bi, "tid %u: seq number %u missing", tid, want_seq);
                ok_ = false;
            }
        }
        if (jmb->tid == tid) {
            tid_flags = jmb->flags;
            if (tid_gt(jmb->commit_boundary, tid)) {
                tid_flags |= jf_commit;
            }
            if (tid_gt(jmb->complete_boundary, tid)) {
                tid_flags |= jf_complete;
            }
            if (!!(tid_flags & jf_start) != (flags == 0)) {
                error(mr_[mi].bi, "tid %u: jf_start flag in improper place", tid);
                ok_ = false;
            }
            if ((flags & jf_commit)
                && jmb->nref != 0) {
                error(mr_[mi].bi, "tid %u: continues after commit", tid);
                ok_ = false;
            }
            if (flags & jf_complete) {
                error(mr_[mi].bi, "tid %u: continues after complete", tid);
                ok_ = false;
            }
            if (tid_flags & jf_complete) {
                error(mr_[mi].bi, "tid %u: completes below complete_boundary", tid);
                ok_ = false;
            }
            flags |= tid_flags;
        }
    }

    if (!(flags & jf_commit)) {
        error(-1U, "tid %u in committable region is not committed", tid);
        ok_ = false;
    }
}

void journalreplayer::analyze_overwritten_blocks(unsigned mx) {
    auto jmb = mr_[mx - 1].b;
    for (unsigned refx = jmb->nref; refx != 0; --refx) {
        auto& ref = jmb->ref[refx - 1];
        auto bflags = from_le(ref.bflags);
        if (!(bflags & jbf_overwritten)) {
            mark_overwritten_block(from_le(ref.bn), mx, refx - 1);
        }
    }
}

void journalreplayer::mark_overwritten_block(blocknum_t bn, unsigned mx,
                                             unsigned refx) {
    for (; mx != 0; --mx, refx = unsigned(-1)) {
        auto jmb = mr_[mx - 1].b;
        if (refx > jmb->nref) {
            refx = jmb->nref;
        }
        for (; refx != 0; --refx) {
            if (from_le(jmb->ref[refx - 1].bn) == bn) {
                auto bflags = from_le(jmb->ref[refx - 1].bflags);
                bflags |= jbf_overwritten;
                jmb->ref[refx - 1].bflags = to_le(bflags);
            }
        }
    }
}


// journalreplayer::run
//    Call `write_*` callbacks to replay journal.

void journalreplayer::run() {
    assert(ok_);
    tid_t complete_boundary = mr_[nmr_ - 1].b->complete_boundary;
    tid_t commit_boundary = mr_[nmr_ - 1].b->commit_boundary;
    for (unsigned mi = 0; mi != nmr_; ++mi) {
        auto jmb = mr_[mi].b;
        if (tid_ge(jmb->tid, complete_boundary)
            && tid_lt(jmb->tid, commit_boundary)) {
            unsigned delta = 1;
            for (unsigned refi = 0; refi != jmb->nref; ++refi) {
                auto& ref = jmb->ref[refi];
                auto bflags = from_le(ref.bflags);
                if (!(bflags & jbf_overwritten)
                    && !(bflags & jbf_nonjournaled)) {
                    auto dbi = (mr_[mi].bi + delta) % nb_;
                    auto djd = jd_ + dbi * blocksize;
                    if (bflags & jbf_escaped) {
                        uint64_t magic = to_le(journalmagic);
                        memcpy(djd, &magic, sizeof(magic));
                    }
                    write_block(jmb->tid, from_le(ref.bn), djd);
                }
                if (!(bflags & jbf_nonjournaled)) {
                    ++delta;
                }
            }
        }
    }
    write_replay_complete();
}


// `run()` callbacks

void journalreplayer::message(blocknum_t, const char*, ...) {
}

void journalreplayer::error(unsigned, const char*, ...) {
}

void journalreplayer::write_block(tid_t, blocknum_t, unsigned char*) {
}

void journalreplayer::write_replay_complete() {
}

}
