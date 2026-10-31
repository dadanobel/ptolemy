#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""OpenBIOS display date in English UTC; deterministic with SOURCE_DATE_EPOCH."""
import datetime,os,re,sys

def build_date(value=None):
    if value is None:
        t=datetime.datetime.now(datetime.timezone.utc)
    else:
        if not re.fullmatch(r"[0-9]{1,20}",value):raise ValueError("SOURCE_DATE_EPOCH must be nonnegative decimal seconds")
        t=datetime.datetime.fromtimestamp(int(value),datetime.timezone.utc)
    month=("Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec")[t.month-1]
    return f"{month} {t.day:2} {t.year:04} {t.hour:02}:{t.minute:02}"

if __name__=="__main__":
    try:print(build_date(os.environ.get("SOURCE_DATE_EPOCH")))
    except (ValueError,OverflowError,OSError) as e:
        print(f"OpenBIOS build date: {e}",file=sys.stderr);sys.exit(2)
