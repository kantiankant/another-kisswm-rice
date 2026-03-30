vim.opt.clipboard = "unnamedplus"
vim.opt.termguicolors = true
vim.opt.fillchars = { eob = " " }
vim.opt.number = true
vim.opt.relativenumber = true

vim.api.nvim_create_autocmd("ColorScheme", {
    pattern = "*",
    callback = function()
        vim.api.nvim_set_hl(0, "Normal",       {fg="#ffffff", bg="NONE"})
        vim.api.nvim_set_hl(0, "NonText",      {bg="NONE"})
        vim.api.nvim_set_hl(0, "EndOfBuffer",  {fg="NONE",   bg="NONE"})
        vim.api.nvim_set_hl(0, "SignColumn",   {bg="NONE"})
        vim.api.nvim_set_hl(0, "LineNr",       {fg="#555555", bg="NONE"})
        vim.api.nvim_set_hl(0, "LineNrAbove",  {fg="#555555", bg="NONE"})
        vim.api.nvim_set_hl(0, "LineNrBelow",  {fg="#555555", bg="NONE"})
        vim.api.nvim_set_hl(0, "CursorLineNr", {fg="#555555", bg="NONE"})
    end
})

vim.cmd("colorscheme bw")

local function render_dashboard(buf)
    if not vim.api.nvim_buf_is_valid(buf) then return end

    local logo = {}
    local logo_path = vim.fn.expand("~/.config/nvim/logo.txt")
    if vim.fn.filereadable(logo_path) == 1 then
        logo = vim.fn.readfile(logo_path)
    end

    local subtitle = "It Sucks Less"

    local menu = {
        "[e] new file",
        "[f] find file",
        "[q] quit",
    }

    local width  = vim.o.columns
    local height = vim.o.lines

    local logo_max = 0
    for _, line in ipairs(logo) do
        if #line > logo_max then logo_max = #line end
    end

    local menu_max = 0
    for _, line in ipairs(menu) do
        if #line > menu_max then menu_max = #line end
    end

    local gap = 3 -- blank lines between subtitle and menu, adjust to taste
    local total_lines = #logo + 2 + gap + #menu
    local top       = math.max(0, math.floor((height - total_lines) / 2))
    local logo_left = math.max(0, math.floor((width - logo_max) / 2))
    local menu_left = math.max(0, math.floor((width - menu_max) / 2))
    local sub_left  = math.max(0, math.floor((width - #subtitle) / 2))

    local padded = {}
    for _ = 1, top do table.insert(padded, "") end

    for _, line in ipairs(logo) do
        table.insert(padded, string.rep(" ", logo_left) .. line)
    end

    table.insert(padded, "")
    table.insert(padded, string.rep(" ", sub_left) .. subtitle)

    for _ = 1, gap do table.insert(padded, "") end

    for _, line in ipairs(menu) do
        table.insert(padded, string.rep(" ", menu_left) .. line)
    end

    while #padded < height do
        table.insert(padded, "")
    end

    vim.bo[buf].modifiable = true
    vim.api.nvim_buf_set_lines(buf, 0, -1, false, padded)
    vim.bo[buf].modifiable = false
end

if vim.fn.argc() == 0 then
    vim.api.nvim_create_autocmd("VimEnter", {
        callback = function()
            local buf = vim.api.nvim_create_buf(false, true)
            vim.api.nvim_set_current_buf(buf)

            render_dashboard(buf)

            vim.api.nvim_create_autocmd("VimResized", {
                buffer = buf,
                callback = function()
                    render_dashboard(buf)
                end
            })

            vim.keymap.set("n", "e", function()
                local path = vim.fn.input("new file: ")
                if path == "" then return end
                path = vim.fn.expand(path)
                local dir = vim.fn.fnamemodify(path, ":h")
                if vim.fn.isdirectory(dir) == 0 then
                    vim.notify("invalid path: " .. dir, vim.log.levels.ERROR)
                    return
                end
                vim.cmd("edit " .. vim.fn.fnameescape(path))
            end, {buffer = buf})

            vim.keymap.set("n", "q", ":qa<CR>", {buffer = buf})
            vim.keymap.set("n", "f", function()
                local tmp = vim.fn.tempname()
                vim.cmd("tabnew")
                local term_buf = vim.api.nvim_get_current_buf()
		vim.fn.termopen("grep -rl '' . --exclude-dir=.git --exclude-dir=node_modules 2>/dev/null | fzf > " .. tmp, {
                    on_exit = function()
                        vim.api.nvim_buf_delete(term_buf, {force = true})
                        local ok, result = pcall(vim.fn.readfile, tmp)
                        if ok and result[1] then
                            local file = vim.trim(result[1])
                            if file ~= "" then
                                vim.cmd("edit " .. vim.fn.fnameescape(file))
                            end
                        end
                    end
                })
                vim.cmd("startinsert")
            end, {buffer = buf})
        end
    })
end
