function send_response()
    -- Set Water Mark of Input Buffer
    ts.http.resp_transform.set_upstream_watermark_bytes(31337)
    local wm = ts.http.resp_transform.get_upstream_watermark_bytes()
    ts.debug(string.format('WMbytes(%d)', wm))
    return 0
end

function do_remap()
    local req_host = ts.client_request.header.Host

    if req_host == nil then
        return 0
    end

    ts.hook(TS_LUA_RESPONSE_TRANSFORM, send_response)
    ts.http.resp_cache_transformed(0)
    ts.http.resp_cache_untransformed(1)
    return 0
end
