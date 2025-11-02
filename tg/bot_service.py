import logging
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
        self.logger = logging.getLogger(__name__)
        
    def copy(self, message):
        self.logger.info(f"执行copy命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            args = [i for i in message.text.split(' ')[1:] if i]
            data = {"cmd":"copy", "name":"", "old":"", "chat_id":message.chat.id}

            if 0<len(args):
                if re.match(r'^[a-zA-Z0-9_-]+$', args[0]):
                    data["name"] = args[0]
                else:
                    # 仅支持字母、数字、下划线、中划线
                    self.bot.reply_to(message, "name format error, only supports letters, numbers, underscores, and hyphens", parse_mode="Markdown")
                    return 
            else:
                self.bot.reply_to(message, "not set name", parse_mode="Markdown")
                return 

            if 1<len(args) and args[1] : 
                data["old"] = args[1]
            self.logger.info(f"copy命令执行成功: {message.text}")
            return data
        except Exception as e:
            self.logger.error(f"copy命令执行失败: {e}")
            raise

    def exec(self, message):
        self.logger.info(f"执行exec命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            def parse_start(ts): # 输入为时间戳或日期
                if ts.isdigit():
                    return int(ts)
                try: 
                    time_format = "%Y-%m-%dT%H:%M:%S"
                    dt = datetime.strptime(ts, time_format)
                    tz = timezone(timedelta(hours=8))
                    dt = dt.replace(tzinfo=tz)
                    timestamp = dt.timestamp()
                    return int(timestamp)
                except ValueError as e:
                    return 0
            def parse_delay(ds):
                if all(ch in "dhms0123456789" for ch in ts): # 以单片机时间为基准做延时
                    delay, num = 0, 0
                    for i in ts:
                        if i.isdigit():
                            num = num*10+int(i)
                        else:
                            if i == 'd':  delay += num*24*60*60
                            elif i == 'h': delay += num*60*60
                            elif i == 'm': delay += num*60
                            else: delay += num
                            num = 0
                    return delay
                return 0
            def parse_freq(cy):
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

            data = {"cmd":"exec", "name":"", "start":0, "delay":0, "freq":0, "cron": "", "remain":1, "taskname":"", "chat_id":message.chat.id}

            if "cron(" in message.text:
                txt = message.text
                args = []
                ns = 0
                while ns<len(txt) and txt[ns] != ' ': ns+=1
                while ns<len(txt) and txt[ns] == ' ': ns+=1
                ne = ns
                while ne<len(txt) and txt[ne] != ' ': ne+=1

                if ns != ne:
                    args.append(txt[ns:ne])
                print (txt[ne:])
                cs, ce = -1, -1
                for i, j in enumerate(txt):
                    if j == '(': cs = i+1
                    if j == ')': ce = i
                    # print(i, j, " = ", cs, ce)
                if cs < ce:
                    args.append(txt[cs:ce])
                # print(args)
                args.extend([i for i in txt[ce+1:].split(' ') if i])
                # print(args)
                
                if 0<len(args) and args[0] : 
                    data["name"] = args[0]
                else:
                    self.bot.reply_to(message, "not set name", parse_mode="Markdown")
                    return 

                if 1<len(args) and args[1] : 
                    data["cron"] = args[1]
                else:
                    self.bot.reply_to(message, "cron expr error", parse_mode="Markdown")
                    return
                
                if 2<len(args) and args[2] :
                    data["remain"] = parse_remain(args[2])
                
                if 3<len(args) and args[3] and len(args[3]) < 128:
                    data["taskname"] = args[3]
                
            else:
                args = [i for i in message.text.split(' ')[1:] if i]
                
                if 0<len(args) and args[0] : 
                    data["name"] = args[0]
                else:
                    self.bot.reply_to(message, "not set name", parse_mode="Markdown")
                    return 
                
                if 1<len(args) and args[1] : 
                    data["start"] = parse_start(args[1])
                    data["delay"] = parse_delay(args[1])
                if 2<len(args) and args[2] : 
                    data["freq"] = parse_freq(args[2])
                if 3<len(args) and args[3] : 
                    data["remain"] = parse_remain(args[3])
                if 4<len(args) and args[4] and len(args[4]) < 128:
                    data["taskname"] = args[4]
            self.logger.info(f"exec命令执行成功: {message.text}")
            return data
        except Exception as e:
            self.logger.error(f"exec命令执行失败: {e}")
            raise

    def terminate(self, message):
        self.logger.info(f"执行terminate命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            result = {"cmd":"terminate", "taskid":" ".join(message.text.split(' ')[1:]), "chat_id":message.chat.id}
            self.logger.info(f"terminate命令执行成功: {message.text}")
            return result
        except Exception as e:
            self.logger.error(f"terminate命令执行失败: {e}")
            raise
    
    def terminatename(self, message):
        self.logger.info(f"执行terminatename命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            result = {"cmd":"terminatename", "taskname":" ".join(message.text.split(' ')[1:]), "chat_id":message.chat.id}
            self.logger.info(f"terminatename命令执行成功: {message.text}")
            return result
        except Exception as e:
            self.logger.error(f"terminatename命令执行失败: {e}")
            raise

    def cmdlist(self, message):
        self.logger.info(f"执行cmdlist命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            result = {"cmd":"cmdlist", "chat_id":message.chat.id}
            self.logger.debug(f"cmdlist命令返回结果数量: {len(result) if result else 0}")
            return result
        except Exception as e:
            self.logger.error(f"cmdlist命令执行失败: {e}")
            raise

    def taskidlist(self, message):
        self.logger.info(f"执行taskidlist命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            result = {"cmd":"taskidlist", "chat_id":message.chat.id}
            self.logger.debug(f"taskidlist命令返回结果数量: {len(result) if result else 0}")
            return result
        except Exception as e:
            self.logger.error(f"taskidlist命令执行失败: {e}")
            raise
        
    def tasklist(self, message):
        self.logger.info(f"执行tasklist命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            result = {"cmd":"tasklist", "chat_id":message.chat.id}
            self.logger.debug(f"tasklist命令返回结果数量: {len(result) if result else 0}")
            return result
        except Exception as e:
            self.logger.error(f"tasklist命令执行失败: {e}")
            raise

    def task(self, message):
        self.logger.info(f"执行task命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            data = {"cmd":"task", "id":-1, "chat_id":message.chat.id}
            args = [i for i in message.text.split(' ')[1:] if i]
            if 0<len(args) and args[0] : 
                data["id"] = args[0]
            else:
                self.bot.reply_to(message, "not set taskid", parse_mode="Markdown")
                return 
            self.logger.info(f"task命令执行成功: {message.text}")
            return data
        except Exception as e:
            self.logger.error(f"task命令执行失败: {e}")
            raise

    def device(self, message):
        self.logger.info(f"执行device命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            args = [i for i in message.text.split(' ')[1:] if i]
            if 0<len(args) and args[0] in self.devices: 
                self.db["device"] = self.devices[args[0]]
                if self.db["device"]["name"] not in self.db["preference"]:
                    self.db["preference"][self.db["device"]["name"]] = {}
                self.bot.reply_to(message, f"device switch to {args[0]}")
                util.save_dict(util.DBFILE, self.db)
            devices_msg = "\n".join([("+ " if k == self.db["device"]["name"] else "- ")+k for k in self.devices])
            self.bot.reply_to(message, f"device list:\n{devices_msg}")
            self.logger.info(f"device命令执行成功，当前设备: {self.db['device']['name']}")
        except Exception as e:
            self.logger.error(f"device命令执行失败: {e}")
            raise

    def usermod(self, message):
        self.logger.info(f"执行usermod命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            if message.chat.id != self.env["ir_admin_chat_id"]:
                self.bot.reply_to(message, f"only administrators can operate")
                return 
            candicates = " ".join(message.text.split(' ')[1:])
            i = 0
            while i < len(candicates):
                while i<len(candicates) and not candicates[i].isdecimal():
                    i += 1
                if i == len(candicates): break
                uid, sub = 0, 0
                if i>0 and candicates[i-1] == '-':
                    sub = 1
                
                while i < len(candicates) and candicates[i].isdecimal():
                    uid = uid*10 + int(candicates[i])
                    i += 1
                if sub:
                    if uid in self.db["user"] and uid != self.env["ir_admin_chat_id"]: self.db["user"].remove(uid)
                else:
                    if uid not in self.db["user"]: self.db["user"].append(uid)

            util.save_dict("db.json", self.db)
            self.bot.reply_to(message, f"user list:\n {str(self.db['user'])}")
            self.logger.info(f"usermod命令执行成功: 用户权限已修改")
        except Exception as e:
            self.logger.error(f"usermod命令执行失败: {e}")
            raise
    
    def auth(self, message):
        self.logger.info(f"执行auth命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            self.bot.send_message(self.env["ir_admin_chat_id"], f"{message.from_user.first_name} {message.from_user.last_name} request chat id: {message.chat.id}")
            self.bot.reply_to(message, f"your application has been submitted to the administrator")
            if message.chat.id in self.db["user"]:
                self.logger.info(f"用户认证成功: user_id={message.from_user.id}")
            else:
                self.logger.warning(f"用户认证失败: user_id={message.from_user.id}")
        except Exception as e:
            self.logger.error(f"auth命令执行失败: {e}")
            raise

    def start(self, message):
        self.logger.info(f"执行start命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            self.bot.send_sticker(chat_id=message.chat.id, sticker="CAACAgQAAxkBAAICVGYZDg7Fg7hZ96S_Wp9t8O26xxxVAAITAwAC2SNkIbQZSopsDmMTNAQ", reply_to_message_id=message.id)
            self.logger.debug("start命令执行完成")
        except Exception as e:
            self.logger.error(f"start命令执行失败: {e}")
            raise
    
    def help(self, message):
        self.logger.info(f"执行help命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        try:
            self.bot.send_sticker(chat_id=message.chat.id, sticker="CAACAgQAAxkBAAICWGYZDmNki3c5DiCYg9impkXVKXP9AAILAwAC2SNkIZ-71pEOj1BjNAQ", reply_to_message_id=message.id)
            if not self.help_text:
                self.help_text = util.load_help()
            self.bot.send_message(message.chat.id, self.help_text, parse_mode="Markdown")
            self.logger.debug("help命令执行完成")
        except Exception as e:
            self.logger.error(f"help命令执行失败: {e}")
            raise