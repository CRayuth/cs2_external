local sep = package.config:sub(1, 1)
local data = vesta.api.data_dir
local live_path = data .. sep .. "live.json"
local request_path = data .. sep .. "request.json"
local applied_path = data .. sep .. "applied_rev.txt"

local CHUNK = 80
local REFRESH_SECONDS = 1.5

local applied = 0
local seq = 0
local co = nil
local last_build = -math.huge

local function read_file(path)
    local file = io.open(path, "rb")
    if not file then return nil end
    local value = file:read("*a")
    file:close()
    return value
end

local function write_atomic(path, value)
    local temp = path .. ".tmp"
    local file = io.open(temp, "wb")
    if not file then return false end
    file:write(value)
    file:close()
    os.remove(path)
    return os.rename(temp, path) ~= nil
end

local function quote(value)
    local escaped = tostring(value)
        :gsub('\\', '\\\\'):gsub('"', '\\"')
        :gsub('\b', '\\b'):gsub('\f', '\\f')
        :gsub('\n', '\\n'):gsub('\r', '\\r'):gsub('\t', '\\t')
    return '"' .. escaped .. '"'
end

local function type_name(value)
    local kind = type(value)
    if kind == "boolean" then return "boolean" end
    if kind == "string" then return "string" end
    if kind == "number" then
        if math.type(value) == "integer" then return "integer" end
        return "number"
    end
    return "null"
end

local function encode_value(value)
    local kind = type(value)
    if value == nil then return "null" end
    if kind == "boolean" then return value and "true" or "false" end
    if kind == "string" then return quote(value) end
    if kind == "number" then
        if value ~= value or value == math.huge or value == -math.huge then return "null" end
        if math.type(value) == "integer" then return string.format("%d", value) end
        return string.format("%.14g", value)
    end
    return "null"
end

local function key_compare(a, b)
    if type(a) == type(b) then return a < b end
    return type(a) == "number"
end

local function json_decode(text)
    local position = 1
    local length = #text
    local parse_value

    local function skip_space()
        while position <= length do
            local c = text:sub(position, position)
            if c == " " or c == "\t" or c == "\n" or c == "\r" then
                position = position + 1
            else
                break
            end
        end
    end

    local function parse_string()
        position = position + 1
        local buffer = {}
        while position <= length do
            local c = text:sub(position, position)
            if c == "\\" then
                local n = text:sub(position + 1, position + 1)
                if n == "n" then buffer[#buffer + 1] = "\n"
                elseif n == "t" then buffer[#buffer + 1] = "\t"
                elseif n == "r" then buffer[#buffer + 1] = "\r"
                elseif n == "b" then buffer[#buffer + 1] = "\b"
                elseif n == "f" then buffer[#buffer + 1] = "\f"
                elseif n == "u" then
                    local code = tonumber(text:sub(position + 2, position + 5), 16) or 63
                    if utf8 and utf8.char then buffer[#buffer + 1] = utf8.char(code)
                    elseif code < 128 then buffer[#buffer + 1] = string.char(code) end
                    position = position + 4
                else buffer[#buffer + 1] = n end
                position = position + 2
            elseif c == "\"" then
                position = position + 1
                return table.concat(buffer)
            else
                buffer[#buffer + 1] = c
                position = position + 1
            end
        end
        return table.concat(buffer)
    end

    local function parse_number()
        local start = position
        while position <= length do
            local c = text:sub(position, position)
            if c:match("[%d%+%.%-%eE]") then position = position + 1 else break end
        end
        return tonumber(text:sub(start, position - 1))
    end

    local function parse_object()
        position = position + 1
        local obj = {}
        skip_space()
        if text:sub(position, position) == "}" then
            position = position + 1
            return obj
        end
        while position <= length do
            skip_space()
            if text:sub(position, position) ~= "\"" then break end
            local key = parse_string()
            skip_space()
            if text:sub(position, position) ~= ":" then break end
            position = position + 1
            obj[key] = parse_value()
            skip_space()
            local delimiter = text:sub(position, position)
            if delimiter == "," then position = position + 1
            elseif delimiter == "}" then position = position + 1 break
            else break end
        end
        return obj
    end

    local function parse_array()
        position = position + 1
        local arr = {}
        skip_space()
        if text:sub(position, position) == "]" then
            position = position + 1
            return arr
        end
        while position <= length do
            arr[#arr + 1] = parse_value()
            skip_space()
            local delimiter = text:sub(position, position)
            if delimiter == "," then position = position + 1
            elseif delimiter == "]" then position = position + 1 break
            else break end
        end
        return arr
    end

    parse_value = function()
        skip_space()
        local c = text:sub(position, position)
        if c == "{" then return parse_object()
        elseif c == "[" then return parse_array()
        elseif c == "\"" then return parse_string()
        elseif c == "t" then position = position + 4 return true
        elseif c == "f" then position = position + 5 return false
        elseif c == "n" then position = position + 4 return nil
        else return parse_number() end
    end

    return parse_value()
end

local function process_request()
    local raw = read_file(request_path)
    if not raw or raw == "" then return end
    local ok, request = pcall(json_decode, raw)
    if not ok or type(request) ~= "table" then return end
    local revision = tonumber(request.rev)
    if not revision or revision == applied then return end
    local edits = request.edits or {}
    for _, edit in ipairs(edits) do
        if type(edit) == "table" and edit.path ~= nil then
            pcall(function() vesta.config.set(tostring(edit.path), edit.value) end)
        end
    end
    applied = revision
    write_atomic(applied_path, tostring(revision))
end

local function build_body()
    local snapshot = vesta.config.snapshot()
    if type(snapshot) ~= "table" then return end
    coroutine.yield()
    local out = {}
    local count = 0
    local budget = 0

    local function walk(node, path)
        local keys = {}
        for key in pairs(node) do keys[#keys + 1] = key end
        table.sort(keys, key_compare)
        for _, key in ipairs(keys) do
            local child = node[key]
            local segment = (type(key) == "number") and (key - 1) or key
            local child_path = path == "" and tostring(segment) or (path .. "." .. tostring(segment))
            if type(child) == "table" then
                walk(child, child_path)
            else
                count = count + 1
                out[count] = string.format('{"path":%s,"type":%s,"value":%s}',
                    quote(child_path), quote(type_name(child)), encode_value(child))
                budget = budget + 1
                if budget >= CHUNK then
                    budget = 0
                    coroutine.yield()
                end
            end
        end
    end

    walk(snapshot, "")
    seq = seq + 1
    local json = '{"seq":' .. seq .. ',"ts":' .. (os.time() * 1000) ..
        ',"applied":' .. applied .. ',"count":' .. count ..
        ',"values":[' .. table.concat(out, ",") .. ']}'
    write_atomic(live_path, json)
end

vesta.events.on("load", function()
    local persisted = read_file(applied_path)
    applied = persisted and tonumber(persisted) or 0
    co = coroutine.create(build_body)
    last_build = os.clock()
end)

vesta.events.on("tick", function(delta)
    process_request()
    if co then
        local ok, err = coroutine.resume(co)
        if not ok then
            vesta.log("device-controller build error: " .. tostring(err))
            co = nil
        elseif coroutine.status(co) == "dead" then
            co = nil
        end
        return
    end
    if os.clock() - last_build >= REFRESH_SECONDS then
        last_build = os.clock()
        co = coroutine.create(build_body)
    end
end)
