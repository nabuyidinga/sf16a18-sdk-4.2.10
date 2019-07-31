--
-- Created by IntelliJ IDEA.
-- User: tommy
-- Date: 2018/6/7
-- Time: 16:42
-- To change this template use File | Settings | File Templates.
--
module("luci.controller.admin.advancednew", package.seeall)
require "luci.tools.webadmin"
local uci = require "luci.model.uci"
local _uci_real  = cursor or _uci_real or uci.cursor()
local nixio = require "nixio"
local sys = require "luci.sys"
local json = require("luci.json")
local sfsys= require("luci.siwifi.networkImpl")
local sysutil = require("luci.siwifi.sf_sysutil")
local ipc = require "luci.ip"
local  networknew = require "luci.controller.admin.networknew"
local sferr = require "luci.siwifi.sf_error"
local fs     = require "nixio.fs"

function index()
	local uci = require("luci.model.uci").cursor()
	local wds_enable = false
	uci:foreach("wireless","wifi-iface",
	function(s)
		if(s["ifname"] == "sfi0" or s["ifname"] == "sfi1"
			or s["ifname"] == "rai0" or s["ifname"] == "rai1") then
			wds_enable = true
		end
	end)

	if (wds_enable == false) then
		entry({"admin", "advancednew"}, firstchild(), _("Advanced"), 62).logo = "advance";
		entry({"admin", "advancednew", "virtual_server"}, template("new_siwifi/senior_user/virtual_server") , _("virtual server"), 1);
		entry({"admin", "advancednew", "dmz"}, template("new_siwifi/senior_user/dmz") , _("DMZ host"), 2);
		entry({"admin", "advancednew", "upnp"}, template("new_siwifi/senior_user/upnp") , _("UPnP setting"), 3);
		entry({"admin", "advancednew", "router"}, template("new_siwifi/senior_user/router") , _("router function"), 4);
		entry({"admin", "advancednew", "ddns"}, template("new_siwifi/senior_user/ddns") , _("DDNS"), 5);
	else
		entry({"admin", "advancednew"}, firstchild());
		entry({"admin", "advancednew", "virtual_server"}, call("goto_default_page"));
		entry({"admin", "advancednew", "dmz"}, call("goto_default_page"));
		entry({"admin", "advancednew", "upnp"}, call("goto_default_page"));
		entry({"admin", "advancednew", "router"}, call("goto_default_page"));
		entry({"admin", "advancednew", "ddns"}, call("goto_default_page"));
	end
	entry({"admin", "advancednew", "get_virtual_server"}, call("get_virtual_server")).leaf = true;
	entry({"admin", "advancednew", "set_virtual_server"}, call("set_virtual_server")).leaf = true;
	entry({"admin", "advancednew", "get_dmz_host"}, call("get_dmz_host")).leaf = true;
	entry({"admin", "advancednew", "set_dmz_host"}, call("set_dmz_host")).leaf = true;
	entry({"admin", "advancednew", "get_UPnP"}, call("get_UPnP")).leaf = true;
	entry({"admin", "advancednew", "set_UPnP"}, call("set_UPnP")).leaf = true;
	entry({"admin", "advancednew", "get_routing_table"}, call("get_routing_table")).leaf = true;
	entry({"admin", "advancednew", "get_static_routing"}, call("get_static_routing")).leaf = true;
	entry({"admin", "advancednew", "set_static_routing"}, call("set_static_routing")).leaf = true;
	entry({"admin", "advancednew", "get_ddns"}, call("get_ddns")).leaf = true;
	entry({"admin", "advancednew", "set_ddns"}, call("set_ddns")).leaf = true;
	--连接设备管理相关接口
	entry({"admin", "advancednew", "get_devices"}, call("get_devices")).leaf = true;
	entry({"admin", "advancednew", "set_device"}, call("set_device")).leaf = true;

end

function goto_default_page()
	luci.http.redirect(luci.dispatcher.build_url())
end

--将table转化为字符串，用于打印
function myprint(params)
	if type(params) ~= "table" then
		return tostring(params)
	end
	local rv = "\n{\n"
	for k, v in pairs(params) do
		rv = rv..tostring(k)..":"..myprint(v)..",\n"
	end
	return string.sub(rv,0,string.len(rv)-2).."\n}\n";
end

function ip_to_mac(ip)
	local mac = nil
	ipc.neighbors({ family = 4 }, function(n)
		if n.mac and n.dest and n.dest:equal(ip) then
			mac = n.mac
		end
	end)
	return mac
end

-- 常用服务器类型 如：http， POP3等 页面使用固定数组映射，系统指关心接口映射
-- when you get the same ip port and different proto, change proto to all
-- in servers
-- externalport
-- internalport
-- ipaddr
-- protocaltype  tcp/udp
function get_virtual_server()
	local servers = {}

	_uci_real:foreach("firewall","redirect",
	function(s)
		if (s["src_dport"] ~= nil) then
			servers[#servers + 1] = {}
			--here is the return data
			servers[#servers]["externalport"] = s["src_dport"]
			servers[#servers]["internalport"] = s["dest_port"]
			servers[#servers]["ipaddr"] = s["dest_ip"]
			servers[#servers]["protocaltype"] = s["proto"]
		end
	end)

	local result = {
		code = 0,
		msg = "OK",
		servers = servers
	}
	sysutil.nx_syslog(myprint(result), 1)
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end

-- externalport
-- internalport
-- ipaddr
-- protocaltype tcp/udp/all
function set_virtual_server()
	local arg_list, data_len = luci.http.content()
	local arg_list_table = json.decode(arg_list)
	local servers = arg_list_table["servers"]
	local fail = 0
	local code = 0

	local result={
		code = 0,
		msg = "OK"
	}

	for i=1, #(servers) do
		if not networknew.check_lan_area(servers[i]["ipaddr"]) then
			fail = 1
			break;
		end
	end

	if fail == 0 then
		_uci_real:foreach("firewall","redirect",
		function(s)
			if(s["src_dport"] ~= nil) then
				_uci_real:delete("firewall",s[".name"])
			end
		end)

		for i=1, #(servers) do
			local s_name = _uci_real:add("firewall", "redirect")
			_uci_real:set("firewall",s_name,"src","wan")

			--here is the set data
			if servers[i]["externalport"] then
				_uci_real:set("firewall",s_name,"src_dport",servers[i]["externalport"])
			end
			if servers[i]["internalport"] then
				_uci_real:set("firewall",s_name,"dest_port",servers[i]["internalport"])
			end
			if servers[i]["ipaddr"] then
				_uci_real:set("firewall",s_name,"dest_ip",servers[i]["ipaddr"])
			end
			if (servers[i]["protocaltype"]  == "all") then
				_uci_real:set("firewall",s_name,"proto","tcp")
				local s_name_2 = _uci_real:add("firewall", "redirect")
				_uci_real:set("firewall",s_name_2,"src","wan")
				if servers[i]["externalport"] then
					_uci_real:set("firewall",s_name_2,"src_dport",servers[i]["externalport"])
				end
				if servers[i]["internalport"] then
					_uci_real:set("firewall",s_name_2,"dest_port",servers[i]["internalport"])
				end
				_uci_real:set("firewall",s_name_2,"dest_ip",servers[i]["ipaddr"])
				_uci_real:set("firewall",s_name_2,"proto","udp")

			else
				if servers[i]["protocaltype"] then
					_uci_real:set("firewall",s_name,"proto",servers[i]["protocaltype"])
				end
			end
		end
		_uci_real:save("firewall")
		_uci_real:commit("firewall")
		luci.util.exec("/etc/init.d/firewall reload")

		sysutil.sflog("INFO","virtual server configure changed!")
	else
		code = sferr.ERROR_NO_IP_NOT_IN_LAN_SEGMENT
	end
	result["code"] = code
	result["msg"]  = sferr.getErrorMessage(code)
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end


-- virtual server and dmz is exclusive
-- todo: not save dmz host ip when change to false now
-- enable(true/false) ipaddr
function get_dmz_host()
	local ipaddr
	local enable = false
	_uci_real:foreach("firewall","redirect",
	function(s)
		if(s["src_dport"] == nil) then
			ipaddr = s["dest_ip"]
			enable = true
		end
	end)

	local result = {
		code = 0,
		msg = "OK",
		enable = enable , --是否关闭DMZ主机
		ipaddr = ipaddr or _uci_real:get("basic_setting","dmz","host") --DMZ主机的ip地址
	}
	sysutil.nx_syslog(myprint(result), 1)
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end

-- enable(true/false) ipaddr
function set_dmz_host()
	local arg_list, data_len = luci.http.content()
	local arg_list_table = json.decode(arg_list)
	local params = arg_list_table["params"]
	local code = 0
	local result = {
		code = 0,
		msg = "OK"
	}

	if (params["ipaddr"] and networknew.check_lan_area(params["ipaddr"])) then
		_uci_real:foreach("firewall","redirect",
		function(s)
			if(s["src_dport"] == nil) then
				_uci_real:delete("firewall",s[".name"])
			end
		end)

		if(params["enable"] == true) then
			local s_name = _uci_real:add("firewall", "redirect")
			_uci_real:set("firewall",s_name,"src","wan")
			_uci_real:set("firewall",s_name,"dest_ip",params["ipaddr"])
			_uci_real:set("firewall",s_name,"proto","all")
		else
			if params["ipaddr"] then
				_uci_real:set("basic_setting","dmz","setting")
				_uci_real:set("basic_setting","dmz","host", params["ipaddr"])
				_uci_real:commit("basic_setting")
			end
		end
	else
		if not params["ipaddr"] and not params["enable"] then
			_uci_real:delete("basic_setting","dmz")
			_uci_real:commit("basic_setting")
		else
			code = sferr.ERROR_NO_IP_NOT_IN_LAN_SEGMENT
		end
	end

	if (result["code"] == 0) then
		_uci_real:save("firewall")
		_uci_real:commit("firewall")
		luci.util.exec("/etc/init.d/firewall reload")
		sysutil.sflog("INFO","dmz configure changed! enable status:%s"%{tostring(params["enable"])})
	end
	result["code"] = code
	result["msg"] = sferr.getErrorMessage(code)
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end

function act_status_si()
	local ipt = io.popen("iptables --line-numbers -t nat -xnvL MINIUPNPD 2>/dev/null")
	if ipt then
		local fwd = { }
		while true do
			local ln = ipt:read("*l")
			if not ln then
				break
			elseif ln:match("^%d+") then
				local num, proto, extport, intaddr, intport =
				ln:match("^(%d+).-([a-z]+).-dpt:(%d+) to:(%S-):(%d+)")

				if num and proto and extport and intaddr and intport then
					num     = tonumber(num)
					extport = tonumber(extport)
					intport = tonumber(intport)

					fwd[#fwd+1] = {
						num     = num,
						proto   = proto:upper(),
						extport = extport,
						intaddr = intaddr,
						intport = intport
					}
				end
			end
		end
		ipt:close()
	end
end

-- now we have not desc and status value
-- todo:for now status and desc is not useful
-- externalport
-- protocaltype
-- internalport
-- internaladdr
-- status
-- desc
function get_UPnP()
	local enable, i
	local applia = {}
	local fwd_rules = {}
	enable = _uci_real:get("upnpd","config","enabled")

	if (enable  == "1") then
		fwd_rules = act_status_si()
		if fwd_rules then
			for i=1, #(fwd_rules) do
				applia[applia + 1] = {}
				applia["externalport"] = fwd_rules["extport"]
				applia["protocaltype"] = fwd_rules["proto"]
				applia["internalport"] = fwd_rules["intport"]
				applia["internaladdr"] = fwd_rules["intaddr"]
				applia["status"] = true
				applia["desc"] = "?"
			end
		end
	end
	local result = {
		code = 0,
		msg = "OK",
		enable = enable, --是否开启UPnP设置
		applia = applia
	}
	sysutil.nx_syslog(myprint(result), 1)
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end

--upnp
--enable(true/false)
function set_UPnP()
	local arg_list, data_len = luci.http.content()
	local arg_list_table = json.decode(arg_list)
	local params = arg_list_table["params"]
	if (params["enable"] == true) then
		_uci_real:set("upnpd","config","enabled","1")
		_uci_real:commit("upnpd")
		luci.util.exec("/etc/init.d/miniupnpd enable")
		luci.util.exec("/etc/init.d/miniupnpd start")

	else
		luci.util.exec("/etc/init.d/miniupnpd stop")
		luci.util.exec("/etc/init.d/miniupnpd disable")
		_uci_real:set("upnpd","config","enabled","0")
		_uci_real:commit("upnpd")
	end
	local result = {
		code = 0,
		msg = "OK"
	}

	sysutil.sflog("INFO","upnp configure changed! enable status:%s"%{tostring(params["enable"])})
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end

function hexip_to_stringip(hex_ip)
	local string_ip = "", string_tmp
	for i=1, string.len(hex_ip), 2 do
		local tmp = tonumber("0x"..string.sub(hex_ip,i,i+1));
		if (i == 1) then
			string_tmp = tostring(tmp)
		else
			string_tmp = tostring(tmp).."."..string_ip
		end
		string_ip = string_tmp
	end
	return string_ip
end

-- targetaddr
-- netmask
-- nextaddr
-- ifacetype
function get_routing_table()
	local route_table = sys.net.routes(nil)
	local routers={}
	for i=1, #(route_table) do
		routers[i] = {
			targetaddr = hexip_to_stringip(route_table[i]["dest"]), --目的网络地址
			netmask = hexip_to_stringip(route_table[i]["mask"]), --子网掩码
			nextaddr = hexip_to_stringip(route_table[i]["gateway"]), --下一跳地址
			ifacetype = luci.tools.webadmin.iface_get_network(route_table[i]["device"]) --接口  "WAN", "LAM" "LAN/WWAN"等
		}
	end
	local result = {
		code = 0,
		msg = "OK",
		routers = routers
	}
	sysutil.nx_syslog(myprint(result), 1)
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end

-- targetaddr
-- netmask
-- nextaddr
function get_static_routing()
	local routers = {}
	_uci_real:foreach("network","route",
	function(s)
		routers[#routers + 1] = {}
		routers[#routers]["nextaddr"] = s["gateway"]
		routers[#routers]["netmask"] = s["netmask"]
		routers[#routers]["targetaddr"] = s["target"]
	end)

	local result = {
		code = 0,
		msg = "OK",
		routers = routers
	}
	sysutil.nx_syslog(myprint(result), 1)
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end

function check_route_area(check_ip, netmask)
	if not check_ip or not netmask then
		return false
	end

	local ip = networknew.StringSplit(check_ip, '%.')
	local mask = networknew.StringSplit(netmask, '%.')
	local bottom_ip ={}
	for k, v in pairs( ip ) do
		if mask[k] == "255" then
			bottom_ip[k] = tonumber(v)
		elseif mask[k] == "0" then
			bottom_ip[k] = 0
		else
			local mod = tonumber(v) % (( 255 -  tonumber(mask[k]) ) + 1 )
			bottom_ip[k] = tonumber(v) - mod
		end
	end
	sysutil.nx_syslog("bottom ip is"..myprint(bottom_ip), 1)

	local check_ip_val = networknew.ip_to_int(check_ip)
	local bottom_ip_val = bottom_ip[1]*2^24 + bottom_ip[2]*2^16 + bottom_ip[3]*2^8 + bottom_ip[4]
	if (check_ip_val ~= bottom_ip_val) then
		return false
	else
		return true
	end
end

-- targetaddr
-- netmask
-- nextaddr
function set_static_routing()
	-- first clean uci config of route
	local arg_list, data_len = luci.http.content()
	local arg_list_table = json.decode(arg_list)
	local routers = arg_list_table["routers"]
	local fail = 0
	local code = 0
	local result = {
		code = 0,
		msg = "OK"
	}

	for i=1, #(routers) do
		if not check_route_area(routers[i]["targetaddr"], routers[i]["netmask"]) then
			code = sferr.ERROR_INPUT_PARAM_ERROR
			fail = 1
			break
		end
	end

	if fail == 0 then
		_uci_real:delete_all("network", "route")
		for i=1, #(routers) do
			local s_name = _uci_real:add("network", "route")
			_uci_real:set("network",s_name,"interface","lan")
			if routers[i]["nextaddr"] then
				_uci_real:set("network",s_name,"gateway",routers[i]["nextaddr"])
			end
			if routers[i]["netmask"] then
				_uci_real:set("network",s_name,"netmask",routers[i]["netmask"])
			end
			if routers[i]["targetaddr"] then
				_uci_real:set("network",s_name,"target",routers[i]["targetaddr"])
			end
		end

		_uci_real:save("network")
		_uci_real:commit("network")
		luci.sys.call("env -i /bin/ubus call network reload >/dev/null 2>/dev/null; sleep 2")
		sysutil.sflog("INFO","static route configure changed!")
	end
	result["code"] = code
	result["msg"]  = sferr.getErrorMessage(code)
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end

function get_ddns()
	local cfg = "ddns"
	local sec = "myddns_ipv4"
	local result = {
		code = 0,
		msg = "OK",
		provider = _uci_real:get(cfg, sec, "service_name"), --服务提供者
		account = _uci_real:get(cfg, sec, "username"), --同户名
		password = _uci_real:get(cfg, sec, "password"), --密码
		autoconnect = _uci_real:get(cfg, sec, "interface") and "1" or "0", --是否自动登录
		domain = _uci_real:get(cfg, sec, "domain")
	}
	sysutil.nx_syslog(myprint(result), 1)
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end

function set_ddns()

	local arg_list, data_len = luci.http.content()
	local arg_list_table = json.decode(arg_list)
	local action = arg_list_table["action"]
	local autoconnect = arg_list_table["autoconnect"]
	local cfg = "ddns"
	local sec = "myddns_ipv4"
	local cmd = "start"


	_uci_real:set(cfg, sec, "interface", autoconnect == "1" and "wan" or "")
	if action == 1 then
		--	local provider = arg_list_table["provider"]
		local account = arg_list_table["account"]
		local password = arg_list_table["password"]
		--	local service = arg_list_table["service"]
		local domain = arg_list_table["domain"]
		_uci_real:set(cfg, sec, "enabled", action)
		if account then
			_uci_real:set(cfg, sec, "username", account)
		end
		if password then
			_uci_real:set(cfg, sec, "password", password)
		end
		if domain then
			_uci_real:set(cfg, sec, "domain", domain)
		end
		--		_uci_real:set(cfg, sec, "service", provider)
		_uci_real:save(cfg)
		_uci_real:commit(cfg)
		os.execute("/usr/lib/ddns/dynamic_dns_lucihelper.sh -S "..sec.." -- "..cmd)
	else
		_uci_real:set(cfg, sec, "enabled", action)
		_uci_real:save(cfg)
		_uci_real:commit(cfg)
		cmd = "stop"
		os.execute("/usr/lib/ddns/dynamic_dns_updater.sh".." -- "..cmd)
	end

	local result = {
		code = 0,
		msg = "OK"
	}

	sysutil.sflog("INFO","ddns configure changed! action:%d"%{action})
	sysutil.nx_syslog(myprint(result), 1)
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end

function get_ap_mac_list()
	local ap_mac_list = {}
	if sysutil.checkFileExist("/etc/config/capwap_devices")  == 1 then
		_uci_real:foreach("capwap_devices", "device", function(s)
			ap_mac_list[#ap_mac_list+1] = s.mac
		end)
	end
	return ap_mac_list
end

--获取在线的设备 和 被禁用的设备
function get_devices()
	--get online device
	--
    local old_time = fs.readfile("/tmp/oldtime")
    if old_time == nil then
        old_time = 0
    end

    local now_time = os.time()
    if now_time - old_time >= 10 then
        local oldtime_f = io.open("/tmp/oldtime", "w")
        oldtime_f:write("%s\n" %{now_time})
        oldtime_f:close()
        local wiredev = sfsys.get_wire_assocdev("0")
        if wiredev then
            sfsys.set_devlist(wiredev)
        end
    end
	sfsys.update_ts()
	local devices = {}
	local dev_list, count = sfsys.get_devinfo_from_devlist(1,nil)
	local manager_mac = ip_to_mac(luci.http.getenv("REMOTE_ADDR")) or ""
	local s = require "luci.tools.status".dhcp_leases()
	local ap_mac_list =  get_ap_mac_list()

	for i=1, #(dev_list) do
		local is_ap = 0
		if #ap_mac_list ~=  0 then
			for j =1, #ap_mac_list do
				if ap_mac_list[j]:upper() == dev_list[i].mac then
					is_ap = 1
				end
			end
		end
		if is_ap == 0 then
			devices[#devices + 1] = {}
			for k, v in pairs(s) do
				if v.macaddr == string.lower(dev_list[i]["mac"]):gsub("_", ":") then
					devices[#devices]["hostname"] = v.hostname --设备名
				end
			end

			if manager_mac == string.lower(dev_list[i]["mac"]):gsub("_", ":") then
				devices[#devices]["display"] = 0 -- 管理PC禁止禁用
			end
			devices[#devices]["mac"] = dev_list[i]["mac"] --mac地址
			devices[#devices]["ip "]=  dev_list[i]["ip"]--ip地址
			devices[#devices]["dev "]= dev_list[i]["dev"] --接入的网络  有线/2.4G/5G
			devices[#devices]["uploadspeed"] = dev_list[i]["speed"]["upspeed"]--当前上行速度
			devices[#devices]["downloadspeed"] = dev_list[i]["speed"]["downspeed"] --当前下行速度
			devices[#devices]["uploadlimit"] = dev_list[i]["authority"]["limitup"]  --上行限速值
			devices[#devices]["downloadlimit"] = dev_list[i]["authority"]["limitdown"] --下行限速值
			devices[#devices]["internet"] = dev_list[i]["authority"]["internet"]  --0 禁用网络 1 不禁用
			devices[#devices]["lan"] = dev_list[i]["authority"]["lan"]  --0 拉黑 1 未拉黑
		end
	end

	local result = {
		code = 0,
		msg = "OK",
		devices = devices
	}
	sysutil.nx_syslog(myprint(result), 1)
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end

function set_device()
	local result = {}
	local arg_list, data_len = luci.http.content()
	local arg_list_table = json.decode(arg_list)
	local mac = arg_list_table["mac"]
	local internet = arg_list_table["internet"]
	local lan = arg_list_table["lan"]
	local speed_ul = arg_list_table["uploadlimit"]
	local speed_dl = arg_list_table["downloadlimit"]
	local code = 0

	if mac then
		local dev_mac = (mac:gsub(":","_")):upper()
		local op_list = sfsys.get_list_by_mac(dev_mac)
		local mac_fmt = dev_mac:gsub("_",":")
		local has_limit = _uci_real:get(op_list, dev_mac,"speedlimit") or 0

		if internet then
			luci.util.exec("aclscript c_net %s %s" %{mac_fmt, tostring(internet)})
			_uci_real:set(op_list, dev_mac, "internet", internet)
		end
		if lan then
			luci.util.exec("aclscript c_lan %s %s" %{mac_fmt, tostring(lan)})
			_uci_real:set(op_list, dev_mac, "lan", lan)
		end

		if speed_ul and speed_dl then
			if speed_ul == -1 and speed_dl == -1 then
				_uci_real:set(op_list, dev_mac, "speedlimit", 0)
				_uci_real:save(op_list)
				_uci_real:commit(op_list)
				luci.util.exec("pctl speed del %s"%{mac_fmt})
			else
				if has_limit == '1' then
					luci.util.exec("pctl speed update %s %s %s"%{mac_fmt, speed_ul, speed_dl})
				else
					luci.util.exec("pctl speed add %s %s %s"%{mac_fmt, speed_ul, speed_dl})
				end
				_uci_real:set(op_list, dev_mac, "speedlimit", 1)
			end

			_uci_real:set(op_list, dev_mac, "limitup", speed_ul)
			_uci_real:set(op_list, dev_mac, "limitdown", speed_dl)
		end
		_uci_real:save(op_list)
		_uci_real:commit(op_list)

		result.code = 0
		result.msg = "OK"
	else
		code = sferr.ERROR_NO_MAC_ADDRESS_RECEIVED
	end
	result["code"] = code
	result["msg"]  = sferr.getErrorMessage(code)
	sysutil.sflog("INFO","device %s configure changed!"%{mac})
	sysutil.nx_syslog(myprint(result), 1)
	luci.http.prepare_content("application/json")
	luci.http.write_json(result)
end
