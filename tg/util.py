import json
from datetime import datetime, timezone, timedelta


DBFILE = "db.json"
ENVFILE = "env.json"

HELPFILE="../README.md"

def load_help():
    try:
        with open(HELPFILE, "r") as f:
            help_text = f.readlines()
        s, e = 0, len(help_text)
        # 找到帮助文本的开始和结束位置
        for i, line in enumerate(help_text):
            if line.startswith("# 机器人指令\n"):
                s = i
            elif line.startswith("# 部署\n"):
                e = i
                break
        return ''.join(help_text[s:e])
    except FileNotFoundError:
        print("Help file not found.")
        return "Help file not found."

def save_dict(name, dc):
    with open(name, 'w') as f:
        json.dump(dc, f, indent=4)

def load_dict(name):
    try:
        with open(name, "r") as f:
            dc = json.load(f)
        return dc
    except FileNotFoundError:
        print(f"文件 '{name}' 不存在。")
        return {}

def dynamic_call(obj, method_name, *args, **kwargs):
    if hasattr(obj, method_name):
        method = getattr(obj, method_name)
        return method(*args, **kwargs)
    raise AttributeError(f"No method '{method_name}'")


def unix_timestamp_to_datetime(timestamp, timezone_hour=8):
    dt = datetime.fromtimestamp(timestamp, tz=timezone(timedelta(hours=timezone_hour)))
    return dt.strftime("%Y-%m-%dT%H:%M:%S")

def seconds_to_hms(seconds):
    m, s = divmod(seconds, 60)
    h, m = divmod(m, 60)
    d, h = divmod(h, 24)
    return f"{d}d{h:02d}h{m:02d}m{s:02d}s"