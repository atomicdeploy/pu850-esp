import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { spawn } from 'node:child_process'
import { promises as fs } from 'node:fs'
import http from 'node:http'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { gzipSync } from 'node:zlib'
import { fileURLToPath } from 'node:url'
import test from 'node:test'
import { EXIT, parseArguments } from './upload.js'

const SCRIPT = fileURLToPath(new URL('./upload.js', import.meta.url))

function md5(data) {
	return createHash('md5').update(data).digest('hex')
}

async function fixture(name = 'firmware.bin') {
	const directory = await fs.mkdtemp(join(tmpdir(), 'asa-upload-test-'))
	const raw = Buffer.from(`test firmware ${Math.random()} ${'x'.repeat(2048)}`)
	const compressed = gzipSync(raw, { mtime: 0 })
	const firmwarePath = join(directory, name)
	const manifestPath = `${firmwarePath}.md5`
	await fs.writeFile(firmwarePath, compressed)
	await fs.writeFile(
		manifestPath,
		`${md5(raw)} *${name}\n${md5(compressed)} *${name} (compressed)\n`
	)
	return {
		directory,
		raw,
		compressed,
		rawMd5: md5(raw),
		compressedMd5: md5(compressed),
		firmwarePath,
		manifestPath,
		async cleanup() {
			await fs.rm(directory, { recursive: true, force: true })
		}
	}
}

async function readBody(request) {
	const chunks = []
	for await (const chunk of request) chunks.push(chunk)
	return Buffer.concat(chunks)
}

function parseMultipart(request, body) {
	const contentType = String(request.headers['content-type'] || '')
	const match = /boundary=([^;]+)/i.exec(contentType)
	assert.ok(match, 'multipart request has a boundary')
	const parts = body.toString('latin1').split(`--${match[1]}`)
	const result = {}
	for (const part of parts) {
		const headerEnd = part.indexOf('\r\n\r\n')
		if (headerEnd < 0) continue
		const headers = part.slice(0, headerEnd)
		let content = part.slice(headerEnd + 4)
		if (content.endsWith('\r\n')) content = content.slice(0, -2)
		if (/name="MD5"/.test(headers)) result.md5 = content
		if (/name="firmware"/.test(headers)) result.firmware = Buffer.from(content, 'latin1')
	}
	return result
}

function infoResponse(response, hash, { json = true } = {}) {
	if (json) {
		const body = JSON.stringify({ hash, size: 1234 })
		response.writeHead(200, {
			'Content-Type': 'application/json',
			'Content-Length': Buffer.byteLength(body)
		})
		response.end(body)
	} else {
		const body = `hostname: test-device\nfirmware hash: ${hash}\nuptime: 1 second\n`
		response.writeHead(200, {
			'Content-Type': 'text/plain',
			'Content-Length': Buffer.byteLength(body)
		})
		response.end(body)
	}
}

function firmwareHeaders(data, extra = {}) {
	const digest = md5(data)
	return {
		'Content-Type': 'application/octet-stream',
		'Content-Length': String(data.length),
		'Accept-Ranges': 'bytes',
		'X-Firmware-MD5': digest,
		'Content-MD5': Buffer.from(digest, 'hex').toString('base64'),
		ETag: `"${digest}"`,
		...extra
	}
}

async function startServer(handler) {
	let contacts = 0
	const server = http.createServer(async (request, response) => {
		contacts++
		try {
			await handler(request, response)
		} catch (error) {
			response.destroy(error)
		}
	})
	await new Promise((resolvePromise, rejectPromise) => {
		server.once('error', rejectPromise)
		server.listen(0, '127.0.0.1', resolvePromise)
	})
	const address = server.address()
	return {
		url: `http://127.0.0.1:${address.port}`,
		get contacts() {
			return contacts
		},
		async close() {
			server.closeAllConnections?.()
			await new Promise(resolvePromise => server.close(resolvePromise))
		}
	}
}

async function runCli(argumentsList, { timeout = 30_000, env = {} } = {}) {
	return await new Promise((resolvePromise, rejectPromise) => {
		const child = spawn(process.execPath, [SCRIPT, ...argumentsList], {
			env: {
				...process.env,
				UPDATE_API: '',
				UPDATE_AUTHORIZATION: '',
				UPDATE_BEARER_TOKEN: '',
				UPDATE_TOKEN: '',
				NO_COLOR: '1',
				...env
			},
			stdio: ['ignore', 'pipe', 'pipe']
		})
		const stdout = []
		const stderr = []
		child.stdout.on('data', chunk => stdout.push(chunk))
		child.stderr.on('data', chunk => stderr.push(chunk))
		const timer = setTimeout(() => {
			child.kill()
			rejectPromise(new Error(`CLI timed out: ${argumentsList.join(' ')}`))
		}, timeout)
		child.once('error', error => {
			clearTimeout(timer)
			rejectPromise(error)
		})
		child.once('exit', (code, signal) => {
			clearTimeout(timer)
			resolvePromise({
				code,
				signal,
				stdout: Buffer.concat(stdout).toString('utf8'),
				stderr: Buffer.concat(stderr).toString('utf8')
			})
		})
	})
}

test('default mode remains watch and --immediate remains compatible', () => {
	assert.equal(parseArguments(['firmware.bin'], { UPDATE_API: 'http://127.0.0.1/update' }).mode, 'watch')
	assert.equal(parseArguments(['--immediate', 'firmware.bin'], { UPDATE_API: 'http://127.0.0.1/update' }).mode, 'immediate')
	assert.equal(parseArguments(['-i', 'firmware.bin'], { UPDATE_API: 'http://127.0.0.1/update' }).mode, 'immediate')
	assert.equal(
		parseArguments(['--download', 'backup.bin', '--url', 'http://127.0.0.1/update'], {}).mode,
		'download'
	)
	assert.throws(
		() => parseArguments(['--download', 'backup.bin', '--immediate'], { UPDATE_API: 'http://127.0.0.1/update' }),
		error => error.exitCode === EXIT.USAGE
	)
})

test('offline check validates both gzip identities without device contact', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	const server = await startServer((_request, response) => {
		response.writeHead(500)
		response.end()
	})
	t.after(() => server.close())

	const result = await runCli([
		'--check', '--url', `${server.url}/update`, '--quiet', files.firmwarePath
	])
	assert.equal(result.code, EXIT.OK, result.stderr)
	assert.equal(server.contacts, 0)
})

test('malformed one-record manifest fails before device contact', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	await fs.writeFile(files.manifestPath, `${files.rawMd5} *firmware.bin\n`)
	const server = await startServer((_request, response) => {
		response.writeHead(500)
		response.end()
	})
	t.after(() => server.close())

	const result = await runCli([
		'--immediate', '--url', `${server.url}/update`, '--quiet', files.firmwarePath
	])
	assert.equal(result.code, EXIT.LOCAL_INPUT)
	assert.equal(server.contacts, 0)
})

test('compressed manifest mismatch is an integrity failure before device contact', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	await fs.writeFile(
		files.manifestPath,
		`${files.rawMd5} *firmware.bin\n${'0'.repeat(32)} *firmware.bin (compressed)\n`
	)
	const server = await startServer((_request, response) => {
		response.writeHead(500)
		response.end()
	})
	t.after(() => server.close())

	const result = await runCli([
		'--immediate', '--url', `${server.url}/update`, '--quiet', files.firmwarePath
	])
	assert.equal(result.code, EXIT.INTEGRITY)
	assert.equal(server.contacts, 0)
})

test('upload performs preflight, verified backup, exact upload, auth, and post-hash verification', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	const oldFirmware = Buffer.from(`old raw firmware ${'o'.repeat(1024)}`)
	const oldHash = md5(oldFirmware)
	const backupPath = join(files.directory, 'backup.bin')
	let currentHash = oldHash
	let upload
	let uploadCount = 0
	const authorizations = []

	const server = await startServer(async (request, response) => {
		authorizations.push(request.headers.authorization)
		if (request.url === '/update/info') {
			infoResponse(response, currentHash)
			return
		}
		if (request.url === '/firmware/download' && request.method === 'HEAD') {
			response.writeHead(200, firmwareHeaders(oldFirmware))
			response.end()
			return
		}
		if (request.url === '/firmware/download' && request.method === 'GET') {
			response.writeHead(200, firmwareHeaders(oldFirmware))
			response.end(oldFirmware)
			return
		}
		if (request.url === '/update' && request.method === 'POST') {
			uploadCount++
			upload = parseMultipart(request, await readBody(request))
			currentHash = files.rawMd5
			response.writeHead(200, { 'Content-Type': 'text/plain', 'Content-Length': '3' })
			response.end('ok!')
			return
		}
		response.writeHead(404)
		response.end()
	})
	t.after(() => server.close())

	const secret = 'top-secret-test-token'
	const result = await runCli([
		'--immediate', '--url', `${server.url}/update`, '--bearer', secret,
		'--backup', backupPath, '--quiet', files.firmwarePath
	])
	assert.equal(result.code, EXIT.OK, result.stderr)
	assert.equal(uploadCount, 1)
	assert.equal(upload.md5, files.compressedMd5)
	assert.deepEqual(upload.firmware, files.compressed)
	assert.deepEqual(await fs.readFile(backupPath), oldFirmware)
	assert.ok(authorizations.length >= 5)
	assert.ok(authorizations.every(value => value === `Bearer ${secret}`))
	assert.doesNotMatch(result.stdout + result.stderr, /top-secret-test-token/)
})

test('identical preflight hash skips upload unless forced', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	let uploads = 0
	const server = await startServer(async (request, response) => {
		if (request.url === '/update/info') {
			infoResponse(response, files.rawMd5)
			return
		}
		if (request.method === 'POST') uploads++
		response.writeHead(500)
		response.end()
	})
	t.after(() => server.close())

	const result = await runCli([
		'--immediate', '--url', `${server.url}/update`, '--quiet', files.firmwarePath
	])
	assert.equal(result.code, EXIT.OK, result.stderr)
	assert.equal(uploads, 0)
	assert.equal(server.contacts, 1)
})

test('--force uploads an identical firmware and --no-verify is explicit', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	let uploads = 0
	const server = await startServer(async (request, response) => {
		if (request.url === '/update/info') {
			infoResponse(response, files.rawMd5)
			return
		}
		if (request.url === '/update' && request.method === 'POST') {
			uploads++
			await readBody(request)
			response.writeHead(200, { 'Content-Length': '3' })
			response.end('ok!')
			return
		}
		response.writeHead(404)
		response.end()
	})
	t.after(() => server.close())

	const result = await runCli([
		'--immediate', '--force', '--no-verify', '--url', `${server.url}/update`,
		'--quiet', files.firmwarePath
	])
	assert.equal(result.code, EXIT.OK, result.stderr)
	assert.equal(uploads, 1)
})

test('upload rejects anything other than the exact ok! body', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	const hostileBody = '\x1b]0;forged title\x07\x1b[31mok!\x1b[0m'
	const server = await startServer(async (request, response) => {
		if (request.url === '/update/info') {
			infoResponse(response, '1'.repeat(32))
			return
		}
		if (request.url === '/update') {
			await readBody(request)
			response.writeHead(200, { 'Content-Length': String(Buffer.byteLength(hostileBody)) })
			response.end(hostileBody)
			return
		}
		response.writeHead(404)
		response.end()
	})
	t.after(() => server.close())

	const result = await runCli([
		'--immediate', '--url', `${server.url}/update`, '--quiet', files.firmwarePath
	])
	assert.equal(result.code, EXIT.UPLOAD_REJECTED)
	assert.doesNotMatch(result.stdout + result.stderr, /\x1b|\x07/)
	assert.match(result.stderr, /ok!/)
})

test('post-reboot hash mismatch fails with the documented verification exit code', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	const oldHash = '2'.repeat(32)
	const server = await startServer(async (request, response) => {
		if (request.url === '/update/info') {
			infoResponse(response, oldHash)
			return
		}
		if (request.url === '/update') {
			await readBody(request)
			response.writeHead(200, { 'Content-Length': '3' })
			response.end('ok!')
			return
		}
		response.writeHead(404)
		response.end()
	})
	t.after(() => server.close())

	const result = await runCli([
		'--immediate', '--url', `${server.url}/update`, '--quiet',
		'--timeout', '200', '--verify-timeout', '350', '--verify-interval', '40',
		files.firmwarePath
	])
	assert.equal(result.code, EXIT.POST_VERIFY)
})

test('info preflight falls back from /update/info to legacy plain /info', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	let currentHash = '3'.repeat(32)
	let legacyInfoCalls = 0
	const server = await startServer(async (request, response) => {
		if (request.url === '/update/info') {
			response.writeHead(404, { 'Content-Length': '0' })
			response.end()
			return
		}
		if (request.url === '/info') {
			legacyInfoCalls++
			infoResponse(response, currentHash, { json: false })
			return
		}
		if (request.url === '/update') {
			await readBody(request)
			currentHash = files.rawMd5
			response.writeHead(200, { 'Content-Length': '3' })
			response.end('ok!')
			return
		}
		response.writeHead(404)
		response.end()
	})
	t.after(() => server.close())

	const result = await runCli([
		'--immediate', '--url', `${server.url}/update`, '--quiet', files.firmwarePath
	])
	assert.equal(result.code, EXIT.OK, result.stderr)
	assert.ok(legacyInfoCalls >= 2)
})

test('explicit download verifies length and every advertised MD5 then atomically publishes', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	const downloaded = Buffer.from(`downloaded sketch ${'d'.repeat(4096)}`)
	const target = join(files.directory, 'download.bin')
	const server = await startServer((request, response) => {
		if (request.url === '/update/info') {
			infoResponse(response, md5(downloaded))
			return
		}
		if (request.url === '/firmware/download') {
			response.writeHead(200, firmwareHeaders(downloaded))
			response.end(request.method === 'HEAD' ? undefined : downloaded)
			return
		}
		response.writeHead(404)
		response.end()
	})
	t.after(() => server.close())

	const result = await runCli([
		'--download', target, '--url', `${server.url}/update`, '--quiet'
	])
	assert.equal(result.code, EXIT.OK, result.stderr)
	assert.deepEqual(await fs.readFile(target), downloaded)
	await assert.rejects(fs.stat(`${target}.part`), { code: 'ENOENT' })
})

test('download safely resumes one bounded byte range from an existing .part', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	const downloaded = Buffer.from(`resumable sketch ${'r'.repeat(8192)}`)
	const target = join(files.directory, 'resumed.bin')
	const offset = 777
	await fs.writeFile(`${target}.part`, downloaded.subarray(0, offset))
	let observedRange = null

	const server = await startServer((request, response) => {
		if (request.url === '/update/info') {
			infoResponse(response, md5(downloaded))
			return
		}
		if (request.url === '/firmware/download' && request.method === 'HEAD') {
			response.writeHead(200, firmwareHeaders(downloaded))
			response.end()
			return
		}
		if (request.url === '/firmware/download') {
			observedRange = request.headers.range
			const remaining = downloaded.subarray(offset)
			response.writeHead(206, {
				'Content-Type': 'application/octet-stream',
				'Content-Length': String(remaining.length),
				'Content-Range': `bytes ${offset}-${downloaded.length - 1}/${downloaded.length}`,
				'Accept-Ranges': 'bytes'
			})
			response.end(remaining)
			return
		}
		response.writeHead(404)
		response.end()
	})
	t.after(() => server.close())

	const result = await runCli([
		'--download', target, '--url', `${server.url}/update`, '--quiet'
	])
	assert.equal(result.code, EXIT.OK, result.stderr)
	assert.equal(observedRange, `bytes=${offset}-`)
	assert.deepEqual(await fs.readFile(target), downloaded)
})

test('corrupt download fails integrity, publishes nothing, and removes poisoned partial file', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	const expected = Buffer.from(`expected sketch ${'e'.repeat(1024)}`)
	const corrupt = Buffer.from(expected)
	corrupt[10] ^= 0xff
	const target = join(files.directory, 'corrupt.bin')
	const server = await startServer((request, response) => {
		if (request.url === '/update/info') {
			infoResponse(response, md5(expected))
			return
		}
		if (request.url === '/firmware/download') {
			response.writeHead(200, {
				...firmwareHeaders(expected),
				'Content-Length': String(corrupt.length)
			})
			response.end(request.method === 'HEAD' ? undefined : corrupt)
			return
		}
		response.writeHead(404)
		response.end()
	})
	t.after(() => server.close())

	const result = await runCli([
		'--download', target, '--url', `${server.url}/update`, '--quiet'
	])
	assert.equal(result.code, EXIT.INTEGRITY)
	await assert.rejects(fs.stat(target), { code: 'ENOENT' })
	await assert.rejects(fs.stat(`${target}.part`), { code: 'ENOENT' })
})

test('download rejects a false Content-Length before publishing', async t => {
	const files = await fixture()
	t.after(() => files.cleanup())
	const downloaded = Buffer.from(`short response ${'s'.repeat(512)}`)
	const target = join(files.directory, 'short.bin')
	const server = await startServer((request, response) => {
		if (request.url === '/update/info') {
			infoResponse(response, md5(downloaded))
			return
		}
		if (request.url === '/firmware/download' && request.method === 'HEAD') {
			response.writeHead(200, firmwareHeaders(downloaded))
			response.end()
			return
		}
		if (request.url === '/firmware/download') {
			response.writeHead(200, {
				...firmwareHeaders(downloaded),
				'Content-Length': String(downloaded.length - 1)
			})
			response.end(downloaded.subarray(0, -1))
			return
		}
		response.writeHead(404)
		response.end()
	})
	t.after(() => server.close())

	const result = await runCli([
		'--download', target, '--url', `${server.url}/update`, '--quiet'
	])
	assert.equal(result.code, EXIT.INTEGRITY)
	await assert.rejects(fs.stat(target), { code: 'ENOENT' })
})
