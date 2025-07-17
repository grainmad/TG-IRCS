from datetime import datetime, timezone, timedelta
import re
import time
import util

class Service:
    def __init__(self, bot, env, devices, db):
        self.bot = bot
        self.env = env
        self.devices = devices
        self.db = db
        self.help_text = None
        
    def copy(self, message):
        args = [i for i in message.text.split(' ')[1:] if i]
        data = {"cmd":"copy", "name":"", "old":"", "chat_id":message.chat.id}

        if 0<len(args):
            if re.match(r'^[a-zA-Z0-9_-]+$', args[0]):
                data["name"] = args[0]
            else:
                # self.bot.reply_to(message, "name format error", parse_mode="Markdown") 
                # 仅支持字母、数字、下划线、中划线
                self.bot.reply_to(message, "name format error, only supports letters, numbers, underscores, and hyphens", parse_mode="Markdown")
                return 
        else:
            self.bot.reply_to(message, "not set name", parse_mode="Markdown")
            return 

        if 1<len(args) and args[1] : 
            data["old"] = args[1]
        return data

    def exec(self, message):
        args = [i for i in message.text.split(' ')[1:] if i]
        data = {"cmd":"exec", "name":"", "start":0, "freq":0, "remain":1,"chat_id":message.chat.id}
        if 0<len(args) and args[0] : 
            data["name"] = args[0]
        else:
            self.bot.reply_to(message, "not set name", parse_mode="Markdown")
            return 

        def parse_start(ts):
            # 时间戳
            if ts.isdigit():
                return int(ts)
            # ?d?h?m?s
            if all(ch in "dhms0123456789" for ch in ts):
                cur = int(time.time())
                num = 0
                for i in ts:
                    if i.isdigit():
                        num = num*10+int(i)
                    else:
                        if i == 'd': cur += num*24*60*60
                        elif i == 'h': cur += num*60*60
                        elif i == 'm': cur += num*60
                        else: cur += num
                        num = 0
                return cur
            # "%Y-%m-%d/%H:%M:%S"
            try:
                time_format = "%Y-%m-%d/%H:%M:%S" # 定义时间格式
                dt = datetime.strptime(ts, time_format) # 解析时间字符串
                tz = timezone(timedelta(hours=8)) # 定义东八区时区（UTC+8）
                dt = dt.replace(tzinfo=tz) # 设置时区
                timestamp = dt.timestamp() # 获取时间戳
                return int(timestamp)
            except ValueError as e:
                return 0
        def parse_freq(cy):
            # ?d?h?m?s
            if all(ch in "dhms0123456789" for ch in cy):
                sc = 0
                num = 0
                for i in cy:
                    if i.isdigit():
                        num = num*10+int(i)
                    else:
                        if i == 'd': sc += num*24*60*60
                        elif i == 'h': sc += num*60*60
                        elif i == 'm': sc += num*60
                        else: sc += num
                        num = 0
                return sc
            return 0
        def parse_remain(rm):
            if rm.isdigit():
                return int(rm)
            return 1
        if 1<len(args) and args[1] : 
            data["start"] = parse_start(args[1])
        if 2<len(args) and args[2] : 
            data["freq"] = parse_freq(args[2])
        if 3<len(args) and args[3] : 
            data["remain"] = parse_remain(args[3])
        
        return data

    def terminate(self, message):
        return {"cmd":"terminate", "taskid":",".join(message.text.split(' ')[1:]), "chat_id":message.chat.id}

    def cmdlist(self, message):
        return {"cmd":"cmdlist", "chat_id":message.chat.id}

    def taskidlist(self, message):
        return {"cmd":"taskidlist", "chat_id":message.chat.id}
        
    def tasklist(self, message):
        return {"cmd":"tasklist", "chat_id":message.chat.id}

    def task(self, message):
        data = {"cmd":"task", "id":-1, "chat_id":message.chat.id}
        args = [i for i in message.text.split(' ')[1:] if i]
        if 0<len(args) and args[0] : 
            data["id"] = args[0]
        else:
            self.bot.reply_to(message, "not set taskid", parse_mode="Markdown")
            return 
        return data

    def device(self, message):
        args = [i for i in message.text.split(' ')[1:] if i]
        if 0<len(args) and args[0] in self.devices: 
            self.db["device"] = self.devices[args[0]]
            self.bot.reply_to(message, f"device switch to {args[0]}")
            util.save_dict("db.json", self.db)
        devices_msg = "\n".join([("+ " if k == self.db["device"]["name"] else "- ")+k for k in self.devices])
        self.bot.reply_to(message, f"device list:\n{devices_msg}")
        return

    def usermod(self, message):
        if message.chat.id != self.env["ir_admin_chat_id"]:
            self.bot.reply_to(message, f"only administrators can operate")
            return 
        candicates = " ".join(message.text.split(' ')[1:])
        i = 0
        while i < len(candicates):
            while i<len(candicates) and not candicates[i].isdecimal():
                i += 1
            if i == len(candicates): break
            # 找到了第一个数字
            uid, sub = 0, 0
            if i>0 and candicates[i-1] == '-':
                sub = 1
            
            while i < len(candicates) and candicates[i].isdecimal():
                uid = uid*10 + int(candicates[i])
                i += 1
            if sub:
                if uid in self.db["user"] and uid != self.env["ir_admin_chat_id"]: self.db["user"].remove(uid) # 非管理员用户
            else:
                if uid not in self.db["user"]: self.db["user"].append(uid)

        util.save_dict("db.json", self.db)
        self.bot.reply_to(message, f"user list:\n {str(self.db["user"])}")
    
    def auth(self, message):
        self.bot.send_message(self.env["ir_admin_chat_id"], f"{message.from_user.first_name} {message.from_user.last_name} request chat id: {message.chat.id}")
        self.bot.reply_to(message, f"your application has been submitted to the administrator")

    def start(self, message):
        self.bot.send_sticker(chat_id=message.chat.id, sticker="CAACAgQAAxkBAAICVGYZDg7Fg7hZ96S_Wp9t8O26xxxVAAITAwAC2SNkIbQZSopsDmMTNAQ", reply_to_message_id=message.id)
    
    def help(self, message):
        self.bot.send_sticker(chat_id=message.chat.id, sticker="CAACAgQAAxkBAAICWGYZDmNki3c5DiCYg9impkXVKXP9AAILAwAC2SNkIZ-71pEOj1BjNAQ", reply_to_message_id=message.id)
        if not self.help_text:
            self.help_text = util.load_help()
        self.bot.send_message(message.chat.id, self.help_text, parse_mode="Markdown")