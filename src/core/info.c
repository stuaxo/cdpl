/* info.c - drive information as text lines, shared by the Drive Info
 * dialog and /INFO */
#include <stdio.h>
#include "info.h"
#include "mscdex.h"
#include "sb.h"

void msf_fmt(char *b, u32 frames, int with_frames)
{
    int m, s, f;

    frame_msf(frames, &m, &s, &f);
    if (with_frames)
        sprintf(b, "%02d:%02d.%02d", m, s, f);
    else
        sprintf(b, "%02d:%02d", m, s);
}

int info_lines(char l[][INFO_W], int max)
{
    int n = 0, i, st;
    u32 ds = cd_devstatus();
    char upc[16], s1[12], s2[12];
    u8 ch[8];
    audstat_t as;
    qinfo_t q;

    if (max < INFO_MAX)
        return 0;
    sprintf(l[n++], "MSCDEX %u.%02u   %d CD drive(s)   current %c:  device %s",
            cd.version >> 8, cd.version & 0xFF, cd.ndrives, 'A' + cd.cur, cd.devname);
    sprintf(l[n++], "Device status %08lX", ds);
    sprintf(l[n++], "  door %s/%s, %s reads, %s, audio %s",
            (ds & 1) ? "open" : "closed", (ds & 2) ? "unlocked" : "locked",
            (ds & 4) ? "cooked+raw" : "cooked", (ds & 8) ? "r/w" : "read-only",
            (ds & 16) ? "yes" : "no");
    sprintf(l[n++], "  interleave %s, prefetch %s, channel ctl %s, Red Book %s",
            (ds & 32) ? "yes" : "no", (ds & 128) ? "yes" : "no",
            (ds & 256) ? "yes" : "no", (ds & 512) ? "yes" : "no");
    sprintf(l[n++], "Sector size   cooked %d   raw %d", cd_sector_size(0), cd_sector_size(1));
    sprintf(l[n++], "Volume size   %lu sectors", cd_volsize());
    sprintf(l[n++], "Media changed %d (1 no, 0 unknown, -1 yes)", cd_media_changed());
    cd_upc(upc);
    sprintf(l[n++], "UPC/EAN       %s", upc);
    if (cd_chaninfo(ch) == 0)
        sprintf(l[n++], "Channels      in %d/%d/%d/%d  vol %d/%d/%d/%d",
                ch[0], ch[2], ch[4], ch[6], ch[1], ch[3], ch[5], ch[7]);
    else
        sprintf(l[n++], "Channels      (Audio Channel Info failed)");
    msf_fmt(s1, cd.toc.leadout, 1);
    sprintf(l[n++], "TOC           tracks %u-%u, lead-out %s", cd.toc.first,
            cd.toc.last, s1);
    if (cd_audstat(&as) >= 0)
        sprintf(l[n++], "Audio status  paused %u  start %08lX  end %08lX",
                as.paused, as.start, as.end);
    else
        sprintf(l[n++], "Audio status  (failed, status %04X)", cd.last_status);
    st = cd_qinfo(&q);
    if (st >= 0) {
        msf_fmt(s1, MSF(q.rm, q.rs, q.rf), 1);
        msf_fmt(s2, q.abs, 1);
        sprintf(l[n++], "Q-channel     ctl/adr %02X  tno %02X  idx %02X  rel %s  abs %s%s",
                q.ctrl_adr, q.tno, q.index, s1, s2, (st & ST_BUSY) ? "  BUSY" : "");
    } else {
        sprintf(l[n++], "Q-channel     (failed, status %04X)", cd.last_status);
    }
    if (sb.present && sb.type == SB_16)
        sprintf(l[n++], "SB16 mixer    master %d/%d  voice %d/%d  CD %d/%d  (0-31)",
                sb_mixer_read(0x30) >> 3, sb_mixer_read(0x31) >> 3,
                sb_mixer_read(0x32) >> 3, sb_mixer_read(0x33) >> 3,
                sb_mixer_read(0x36) >> 3, sb_mixer_read(0x37) >> 3);
    else if (sb.present && sb.type == SB_PRO)
        sprintf(l[n++], "SB Pro mixer  master %02X  voice %02X  CD %02X",
                sb_mixer_read(0x22), sb_mixer_read(0x04), sb_mixer_read(0x28));
    else
        sprintf(l[n++], "Sound Blaster %s", sb.desc);
    sprintf(l[n++], "Last status   %04X", cd.last_status);
    for (i = 0; i < n; i++)
        l[i][INFO_W - 2] = 0;
    return n;
}
