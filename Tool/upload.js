#!/usr/bin/env node

import { createHash, randomBytes } from 'node:crypto'
import dns from 'node:dns/promises'
import { promises as fs } from 'node:fs'
import http from 'node:http'
import https from 'node:https'
import { isIP } from 'node:net'
import { basename, dirname, resolve } from 'node:path'
import process from 'node:process'
import { gunzipSync } from 'node:zlib'
import { fileURLToPath } from 'node:url'
import chalk from 'chalk'
import { watch } from 'chokidar'

export const EXIT = Object.freeze({
	OK: 0,
	USAGE: 2,
	LOCAL_INPUT: 3,
	NETWORK: 4,
	PROTOCOL: 5,
	INTEGRITY: 6,
	UPLOAD_REJECTED: 7,
	POST_VERIFY: 8,
	LOCAL_IO: 9,
	INTERRUPTED: 130
})

const DEFAULTS = Object.freeze({
	requestTimeoutMs: 10_000,
	verifyTimeoutMs: 30_000,
	verifyIntervalMs: 750,
	maxFirmwareBytes: 16 * 1024 * 1024,
	maxDownloadBytes: 16 * 1024 * 1024
})

const USER_AGENT = 'ASA-Firmware-Transfer/2.0'
const NETWORK_ERROR_CODES = new Set([
	'EAI_AGAIN', 'ENOTFOUND', 'ECONNREFUSED', 'ECONNRESET', 'EPIPE',
	'ETIMEDOUT', 'ENETUNREACH', 'EHOSTUNREACH', 'EADDRNOTAVAIL'
])

class TransferError extends Error {
	constructor(message, exitCode, options = {}) {
		super(message, options)
		this.name = 'TransferError'
		this.exitCode = exitCode
	}
}

class EndpointResolver {
	constructor(updateUrl, logger) {
		this.updateUrl = updateUrl
		this.originalHostname = updateUrl.hostname
		this.address = null
		this.family = 0
		this.refreshUsed = false
		this.logger = logger
	}

	async initialize() {
		if (isIP(this.originalHostname)) {
			this.address = this.originalHostname
			this.family = isIP(this.originalHostname)
			return
		}
		await this.#resolve(false)
	}

	async refreshOnce() {
		if (this.refreshUsed || isIP(this.originalHostname)) return false
		this.refreshUsed = true
		await this.#resolve(true)
		return true
	}

	async #resolve(refresh) {
		try {
			const result = await dns.lookup(this.originalHostname)
			this.address = result.address
			this.family = result.family
			this.logger.detail(`${refresh ? 'Re-resolved' : 'Resolved'} ${this.originalHostname} to ${result.address}`)
		} catch (error) {
			throw new TransferError(
				`Unable to resolve ${this.originalHostname}: ${sanitizeDeviceText(error.message)}`,
				EXIT.NETWORK,
				{ cause: error }
			)
		}
	}

	requestOptions(url, headers = {}) {
		if (url.hostname !== this.originalHostname) {
			throw new TransferError('All firmware endpoints must use the configured device host', EXIT.USAGE)
		}

		return {
			protocol: url.protocol,
			hostname: this.address,
			family: this.family || undefined,
			port: url.port || undefined,
			method: 'GET',
			path: `${url.pathname}${url.search}`,
			servername: this.originalHostname,
			headers: {
				Host: url.host,
				...headers
			},
			agent: false
		}
	}
}

function createLogger({ quiet = false } = {}) {
	return {
		info(message) {
			if (!quiet) console.log(message)
		},
		detail(message) {
			if (!quiet) console.log(chalk.gray(message))
		},
		success(message) {
			if (!quiet) console.log(chalk.green(message))
		},
		warn(message) {
			if (!quiet) console.warn(chalk.yellow(message))
		},
		error(message) {
			console.error(chalk.red(message))
		}
	}
}

function usage() {
	return `${chalk.green.bold('ASA firmware transfer tool')}

Usage:
  node upload.js [options] firmware.bin        Watch firmware (default)
  node upload.js --immediate [options] firmware.bin
  node upload.js --check [options] firmware.bin
  node upload.js --download PATH [options]

Transfer options:
  -i, --immediate          Upload once instead of watching
      --watch              Explicitly select watch mode
      --check              Validate gzip and strict two-record manifest offline
      --force              Upload even when the device already has this raw hash
      --no-verify          Explicitly disable mandatory post-reboot hash verification
      --backup PATH        Atomically download and verify current firmware before upload
      --download PATH      Atomically download and verify firmware, without uploading
      --no-resume          Ignore an existing PATH.part download
      --manifest PATH      Manifest path (default: firmware.bin.md5)

Connection options:
      --url URL            Update endpoint (default: UPDATE_API)
      --authorization VAL  Complete Authorization value (or UPDATE_AUTHORIZATION)
      --bearer TOKEN       Bearer token (or UPDATE_BEARER_TOKEN / UPDATE_TOKEN)
      --timeout MS         Per-request timeout (default: ${DEFAULTS.requestTimeoutMs})
      --verify-timeout MS  Reboot/hash deadline (default: ${DEFAULTS.verifyTimeoutMs})
      --verify-interval MS Poll interval (default: ${DEFAULTS.verifyIntervalMs})
      --max-download-size N  Download safety limit in bytes
      --max-firmware-size N  Local compressed/raw safety limit in bytes
      --quiet              Suppress informational output
  -h, --help               Show this help

The update URL must include /update, for example:
  UPDATE_API=http://device.local/update node upload.js --immediate Build/ASA0002E.ino.bin

Exit codes: 0 success/no-op, 2 usage, 3 local input/manifest, 4 network,
5 device protocol, 6 integrity, 7 upload rejected, 8 post-verify, 9 local I/O,
130 interrupted.`
}

function optionValue(argv, index, inlineValue, option) {
	if (inlineValue !== undefined) return [inlineValue, index]
	if (index + 1 >= argv.length) {
		throw new TransferError(`${option} requires a value`, EXIT.USAGE)
	}
	return [argv[index + 1], index + 1]
}

export function parseArguments(argv, env = process.env) {
	const config = {
		mode: null,
		firmwarePath: null,
		manifestPath: null,
		updateUrlText: env.UPDATE_API || '',
		authorization: null,
		backupPath: null,
		downloadPath: null,
		force: false,
		verify: true,
		resume: true,
		quiet: false,
		help: false,
		...DEFAULTS
	}

	let explicitAuthorization = null
	let bearerToken = null
	const positional = []
	const selectMode = mode => {
		if (config.mode && config.mode !== mode) {
			throw new TransferError(`Conflicting modes: --${config.mode} and --${mode}`, EXIT.USAGE)
		}
		config.mode = mode
	}

	for (let index = 0; index < argv.length; index++) {
		const argument = argv[index]
		if (argument === '--') {
			positional.push(...argv.slice(index + 1))
			break
		}

		const equals = argument.indexOf('=')
		const name = equals >= 0 ? argument.slice(0, equals) : argument
		const inlineValue = equals >= 0 ? argument.slice(equals + 1) : undefined

		switch (name) {
			case '-h':
			case '--help':
				config.help = true
				break
			case '-i':
			case '--immediate':
				selectMode('immediate')
				break
			case '--watch':
				selectMode('watch')
				break
			case '--check':
				selectMode('check')
				break
			case '--force':
				config.force = true
				break
			case '--no-verify':
				config.verify = false
				break
			case '--no-resume':
				config.resume = false
				break
			case '--quiet':
				config.quiet = true
				break
			case '--url': {
				const [value, next] = optionValue(argv, index, inlineValue, name)
				config.updateUrlText = value
				index = next
				break
			}
			case '--manifest': {
				const [value, next] = optionValue(argv, index, inlineValue, name)
				config.manifestPath = value
				index = next
				break
			}
			case '--backup': {
				const [value, next] = optionValue(argv, index, inlineValue, name)
				config.backupPath = value
				index = next
				break
			}
			case '--download': {
				const [value, next] = optionValue(argv, index, inlineValue, name)
				config.downloadPath = value
				selectMode('download')
				index = next
				break
			}
			case '--authorization': {
				const [value, next] = optionValue(argv, index, inlineValue, name)
				explicitAuthorization = value
				index = next
				break
			}
			case '--bearer': {
				const [value, next] = optionValue(argv, index, inlineValue, name)
				bearerToken = value
				index = next
				break
			}
			case '--timeout':
			case '--verify-timeout':
			case '--verify-interval':
			case '--max-download-size':
			case '--max-firmware-size': {
				const [value, next] = optionValue(argv, index, inlineValue, name)
				const number = Number(value)
				if (!Number.isSafeInteger(number) || number <= 0) {
					throw new TransferError(`${name} must be a positive integer`, EXIT.USAGE)
				}
				const property = {
					'--timeout': 'requestTimeoutMs',
					'--verify-timeout': 'verifyTimeoutMs',
					'--verify-interval': 'verifyIntervalMs',
					'--max-download-size': 'maxDownloadBytes',
					'--max-firmware-size': 'maxFirmwareBytes'
				}[name]
				config[property] = number
				index = next
				break
			}
			default:
				if (argument.startsWith('-')) {
					throw new TransferError(`Unknown option: ${name}`, EXIT.USAGE)
				}
				positional.push(argument)
		}
	}

	if (config.help) return config
	config.mode ||= 'watch'
	if (positional.length > 1) {
		throw new TransferError('Only one firmware path may be supplied', EXIT.USAGE)
	}
	config.firmwarePath = positional[0] || null

	if (config.mode === 'download') {
		if (config.firmwarePath) throw new TransferError('--download does not accept a firmware input', EXIT.USAGE)
		if (config.backupPath) throw new TransferError('--backup cannot be combined with --download', EXIT.USAGE)
	} else if (!config.firmwarePath) {
		throw new TransferError('A firmware file path is required', EXIT.USAGE)
	}

	if (config.backupPath && config.mode === 'check') {
		throw new TransferError('--backup cannot be combined with --check', EXIT.USAGE)
	}
	if (!config.manifestPath && config.firmwarePath) {
		config.manifestPath = `${config.firmwarePath}.md5`
	}

	if (explicitAuthorization && bearerToken) {
		throw new TransferError('Use either --authorization or --bearer, not both', EXIT.USAGE)
	}
	if (!explicitAuthorization && !bearerToken) {
		explicitAuthorization = env.UPDATE_AUTHORIZATION || null
		bearerToken = env.UPDATE_BEARER_TOKEN || env.UPDATE_TOKEN || null
	}
	if (explicitAuthorization && bearerToken) {
		throw new TransferError('Configure either UPDATE_AUTHORIZATION or a bearer token, not both', EXIT.USAGE)
	}
	config.authorization = explicitAuthorization || (bearerToken ? `Bearer ${bearerToken}` : null)
	if (config.authorization && (/[\r\n]/.test(config.authorization) || config.authorization.length > 4096)) {
		throw new TransferError('Invalid Authorization value', EXIT.USAGE)
	}

	if (config.mode !== 'check') {
		if (!config.updateUrlText) {
			throw new TransferError('Provide --url or the UPDATE_API environment variable', EXIT.USAGE)
		}
		let updateUrl
		try {
			updateUrl = new URL(config.updateUrlText)
		} catch {
			throw new TransferError('The update API is not a valid URL', EXIT.USAGE)
		}
		if (!['http:', 'https:'].includes(updateUrl.protocol)) {
			throw new TransferError('The update API must use HTTP or HTTPS', EXIT.USAGE)
		}
		if (updateUrl.username || updateUrl.password) {
			throw new TransferError('URL credentials are not supported; use an Authorization option', EXIT.USAGE)
		}
		updateUrl.hash = ''
		config.updateUrl = updateUrl
	}

	return config
}

function normalizeManifestName(name) {
	return name.replaceAll('\\', '/').split('/').at(-1)
}

export function parseStrictManifest(text, firmwareName) {
	const normalized = text.replace(/\r\n/g, '\n').replace(/\r/g, '\n').trimEnd()
	const lines = normalized.split('\n')
	if (lines.length !== 2 || lines.some(line => !line)) {
		throw new TransferError('Manifest must contain exactly two non-empty md5sum records', EXIT.LOCAL_INPUT)
	}

	const records = new Map()
	for (const line of lines) {
		const match = /^([0-9a-fA-F]{32}) ([ *])(.+)$/.exec(line)
		if (!match) {
			throw new TransferError('Manifest contains a malformed md5sum record', EXIT.LOCAL_INPUT)
		}
		const name = normalizeManifestName(match[3])
		const hash = match[1].toLowerCase()
		if (records.has(name)) {
			throw new TransferError(`Manifest contains a duplicate record for ${name}`, EXIT.LOCAL_INPUT)
		}
		records.set(name, hash)
	}

	const expectedNames = new Set([firmwareName, `${firmwareName} (compressed)`])
	if (records.size !== expectedNames.size || [...records.keys()].some(name => !expectedNames.has(name))) {
		throw new TransferError(
			`Manifest must name exactly "${firmwareName}" and "${firmwareName} (compressed)"`,
			EXIT.LOCAL_INPUT
		)
	}

	return {
		rawMd5: records.get(firmwareName),
		compressedMd5: records.get(`${firmwareName} (compressed)`)
	}
}

function md5Hex(data) {
	return createHash('md5').update(data).digest('hex')
}

async function readFileAsInput(path, label) {
	try {
		return await fs.readFile(path)
	} catch (error) {
		throw new TransferError(
			`Unable to read ${label} "${path}": ${sanitizeDeviceText(error.message)}`,
			EXIT.LOCAL_INPUT,
			{ cause: error }
		)
	}
}

export async function prepareFirmware(config) {
	const firmwarePath = resolve(config.firmwarePath)
	const manifestPath = resolve(config.manifestPath)
	const compressed = await readFileAsInput(firmwarePath, 'firmware')
	if (compressed.length === 0 || compressed.length > config.maxFirmwareBytes) {
		throw new TransferError('Compressed firmware is empty or exceeds the configured safety limit', EXIT.LOCAL_INPUT)
	}

	let raw
	try {
		raw = gunzipSync(compressed, { maxOutputLength: config.maxFirmwareBytes })
	} catch (error) {
		throw new TransferError(
			`Firmware is not a valid bounded gzip stream: ${sanitizeDeviceText(error.message)}`,
			EXIT.LOCAL_INPUT,
			{ cause: error }
		)
	}
	if (raw.length === 0 || raw.length > config.maxFirmwareBytes) {
		throw new TransferError('Raw firmware is empty or exceeds the configured safety limit', EXIT.LOCAL_INPUT)
	}

	const manifestText = (await readFileAsInput(manifestPath, 'manifest')).toString('utf8')
	const manifest = parseStrictManifest(manifestText, basename(firmwarePath))
	const calculatedCompressedMd5 = md5Hex(compressed)
	const calculatedRawMd5 = md5Hex(raw)

	if (manifest.compressedMd5 !== calculatedCompressedMd5) {
		throw new TransferError(
			`Compressed firmware MD5 mismatch (manifest ${manifest.compressedMd5}, calculated ${calculatedCompressedMd5})`,
			EXIT.INTEGRITY
		)
	}
	if (manifest.rawMd5 !== calculatedRawMd5) {
		throw new TransferError(
			`Raw firmware MD5 mismatch (manifest ${manifest.rawMd5}, calculated ${calculatedRawMd5})`,
			EXIT.INTEGRITY
		)
	}

	return {
		firmwarePath,
		manifestPath,
		fileName: basename(firmwarePath),
		compressed,
		compressedMd5: calculatedCompressedMd5,
		rawMd5: calculatedRawMd5,
		rawSize: raw.length
	}
}

function sanitizeDeviceText(value, maxLength = 2048) {
	return String(value ?? '')
		.replace(/\x1B(?:[@-_][0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1B\\)?)/g, '')
		.replace(/[\x00-\x08\x0B\x0C\x0E-\x1F\x7F-\x9F]/g, '')
		.slice(0, maxLength)
}

function isRetryableNetworkError(error) {
	return error?.exitCode === EXIT.NETWORK || NETWORK_ERROR_CODES.has(error?.code)
}

function commonHeaders(config) {
	return {
		'User-Agent': USER_AGENT,
		...(config.authorization ? { Authorization: config.authorization } : {})
	}
}

async function requestAttempt(resolver, url, {
	method = 'GET',
	headers = {},
	body = null,
	timeoutMs,
	maxResponseBytes
}) {
	const transport = url.protocol === 'https:' ? https : http
	const options = resolver.requestOptions(url, headers)
	options.method = method

	return await new Promise((resolvePromise, rejectPromise) => {
		let bodyCommitted = false
		let settled = false
		let timer

		const finishError = error => {
			if (settled) return
			settled = true
			clearTimeout(timer)
			error.bodyCommitted = bodyCommitted
			if (error instanceof TransferError) rejectPromise(error)
			else {
				const wrapped = new TransferError(
					`Network request failed: ${sanitizeDeviceText(error.message)}`,
					EXIT.NETWORK,
					{ cause: error }
				)
				wrapped.code = error.code
				wrapped.bodyCommitted = bodyCommitted
				rejectPromise(wrapped)
			}
		}

		const request = transport.request(options, response => {
			const chunks = []
			let received = 0
			response.on('data', chunk => {
				received += chunk.length
				if (received > maxResponseBytes) {
					response.destroy(new TransferError('Response exceeds the configured safety limit', EXIT.PROTOCOL))
					return
				}
				chunks.push(chunk)
			})
			response.on('error', finishError)
			response.on('end', () => {
				if (settled) return
				settled = true
				clearTimeout(timer)
				resolvePromise({
					statusCode: response.statusCode || 0,
					headers: response.headers,
					body: Buffer.concat(chunks)
				})
			})
		})
		request.on('error', finishError)

		timer = setTimeout(() => {
			const timeoutError = new Error(`Request timed out after ${timeoutMs} ms`)
			timeoutError.code = 'ETIMEDOUT'
			request.destroy(timeoutError)
		}, timeoutMs)

		if (body === null) {
			request.end()
			return
		}

		const commitBody = () => {
			if (settled || bodyCommitted) return
			bodyCommitted = true
			request.end(body)
		}

		request.once('socket', socket => {
			if (!socket.connecting) {
				commitBody()
				return
			}
			socket.once(url.protocol === 'https:' ? 'secureConnect' : 'connect', commitBody)
		})
	})
}

async function requestBuffer(resolver, url, options) {
	try {
		return await requestAttempt(resolver, url, options)
	} catch (error) {
		const canRefresh = options.safeRefresh && isRetryableNetworkError(error) && !error.bodyCommitted
		if (!canRefresh || !(await resolver.refreshOnce())) throw error
		return await requestAttempt(resolver, url, options)
	}
}

function endpoint(updateUrl, path) {
	return new URL(path, `${updateUrl.protocol}//${updateUrl.host}/`)
}

function statusBody(response) {
	const body = sanitizeDeviceText(response.body.toString('utf8')).trim()
	return body ? `: ${body}` : ''
}

function parseHashCandidate(value) {
	const match = String(value ?? '').match(/\b([0-9a-fA-F]{32})\b/)
	return match ? match[1].toLowerCase() : null
}

export function parseInfoHash(body, contentType = '') {
	const text = sanitizeDeviceText(body.toString('utf8'), 64 * 1024)
	if (contentType.includes('json') || text.trimStart().startsWith('{')) {
		try {
			const object = JSON.parse(text)
			for (const key of ['hash', 'firmwareHash', 'firmware_hash', 'md5', 'firmware md5']) {
				const hash = parseHashCandidate(object?.[key])
				if (hash) return hash
			}
		} catch {
			// The legacy plain-line parser below is intentionally the fallback.
		}
	}

	for (const line of text.split(/\r?\n/)) {
		const separator = line.indexOf(':')
		if (separator < 0) continue
		const key = line.slice(0, separator).trim().toLowerCase()
		if (!['hash', 'firmware hash', 'firmware md5', 'md5'].includes(key)) continue
		const hash = parseHashCandidate(line.slice(separator + 1))
		if (hash) return hash
	}
	return null
}

async function fetchFirmwareInfo(resolver, config, { polling = false, deadline = null } = {}) {
	const candidates = [endpoint(config.updateUrl, '/update/info'), endpoint(config.updateUrl, '/info')]
	let firstFailure = null

	for (let index = 0; index < candidates.length; index++) {
		const remaining = deadline === null ? config.requestTimeoutMs : deadline - Date.now()
		if (remaining <= 0) {
			throw new TransferError('Firmware information deadline expired', EXIT.NETWORK)
		}
		const response = await requestBuffer(resolver, candidates[index], {
			headers: commonHeaders(config),
			timeoutMs: Math.min(config.requestTimeoutMs, remaining),
			maxResponseBytes: 256 * 1024,
			safeRefresh: polling || index === 0
		})

		if (response.statusCode >= 200 && response.statusCode < 300) {
			const hash = parseInfoHash(response.body, String(response.headers['content-type'] || ''))
			if (hash) return { hash, endpoint: candidates[index], response }
			firstFailure ||= new TransferError(
				`${candidates[index].pathname} did not contain a valid firmware hash`,
				EXIT.PROTOCOL
			)
			continue
		}

		if (index === 0 && [404, 405, 501].includes(response.statusCode)) continue
		throw new TransferError(
			`${candidates[index].pathname} returned HTTP ${response.statusCode}${statusBody(response)}`,
			EXIT.PROTOCOL
		)
	}

	throw firstFailure || new TransferError('The device did not report a firmware hash', EXIT.PROTOCOL)
}

function multipartBody(firmware) {
	const boundary = `----------------ASA${randomBytes(16).toString('hex')}`
	const safeName = firmware.fileName.replace(/["\r\n]/g, '_')
	const prefix = Buffer.from(
		`--${boundary}\r\n` +
		'Content-Disposition: form-data; name="MD5"\r\n\r\n' +
		`${firmware.compressedMd5}\r\n` +
		`--${boundary}\r\n` +
		`Content-Disposition: form-data; name="firmware"; filename="${safeName}"\r\n` +
		'Content-Type: application/octet-stream\r\n\r\n',
		'utf8'
	)
	const suffix = Buffer.from(`\r\n--${boundary}--\r\n`, 'utf8')
	return {
		boundary,
		body: Buffer.concat([prefix, firmware.compressed, suffix])
	}
}

async function uploadCompressedFirmware(resolver, config, firmware) {
	const multipart = multipartBody(firmware)
	const headers = {
		...commonHeaders(config),
		'Content-Type': `multipart/form-data; boundary=${multipart.boundary}`,
		'Content-Length': String(multipart.body.length)
	}
	const response = await requestBuffer(resolver, config.updateUrl, {
		method: 'POST',
		headers,
		body: multipart.body,
		timeoutMs: config.requestTimeoutMs,
		maxResponseBytes: 256 * 1024,
		safeRefresh: true
	})

	if (response.statusCode < 200 || response.statusCode >= 300) {
		throw new TransferError(
			`Upload returned HTTP ${response.statusCode}${statusBody(response)}`,
			EXIT.UPLOAD_REJECTED
		)
	}
	if (!response.body.equals(Buffer.from('ok!'))) {
		throw new TransferError(
			`Upload response was not the exact acknowledgement "ok!"${statusBody(response)}`,
			EXIT.UPLOAD_REJECTED
		)
	}
}

function delay(milliseconds) {
	return new Promise(resolvePromise => setTimeout(resolvePromise, milliseconds))
}

async function verifyAfterReboot(resolver, config, expectedRawMd5, logger) {
	const deadline = Date.now() + config.verifyTimeoutMs
	let lastHash = null
	let lastError = null

	while (Date.now() <= deadline) {
		try {
			const info = await fetchFirmwareInfo(resolver, config, { polling: true, deadline })
			lastHash = info.hash
			if (lastHash === expectedRawMd5) return
			lastError = null
		} catch (error) {
			lastError = error
		}

		const remaining = deadline - Date.now()
		if (remaining <= 0) break
		logger.detail('Waiting for the device to reboot and report the new hash…')
		await delay(Math.min(config.verifyIntervalMs, remaining))
	}

	const detail = lastHash
		? `last reported ${lastHash}`
		: `last error: ${sanitizeDeviceText(lastError?.message || 'no response')}`
	throw new TransferError(
		`Post-reboot firmware verification failed; expected ${expectedRawMd5}, ${detail}`,
		EXIT.POST_VERIFY
	)
}

function parseContentLength(headers, label) {
	const value = headers['content-length']
	if (typeof value !== 'string' || !/^\d+$/.test(value)) {
		throw new TransferError(`${label} omitted a valid Content-Length`, EXIT.PROTOCOL)
	}
	const number = Number(value)
	if (!Number.isSafeInteger(number) || number < 0) {
		throw new TransferError(`${label} supplied an invalid Content-Length`, EXIT.PROTOCOL)
	}
	return number
}

function parseHeaderMd5(headers, name) {
	const value = headers[name]
	if (value === undefined) return null
	if (Array.isArray(value)) {
		throw new TransferError(`Multiple ${name} headers are not allowed`, EXIT.PROTOCOL)
	}

	if (name === 'content-md5') {
		const normalized = String(value).trim()
		if (!/^[A-Za-z0-9+/]{22}==$/.test(normalized)) {
			throw new TransferError('Content-MD5 is not a valid MD5 digest', EXIT.PROTOCOL)
		}
		const decoded = Buffer.from(normalized, 'base64')
		if (decoded.length !== 16) {
			throw new TransferError('Content-MD5 is not a 16-byte MD5 digest', EXIT.PROTOCOL)
		}
		return decoded.toString('hex')
	}

	if (name === 'etag') {
		const match = /^(?:W\/)?"([0-9a-fA-F]{32})"$/.exec(String(value).trim())
		if (!match) throw new TransferError('ETag is present but is not a firmware MD5', EXIT.PROTOCOL)
		return match[1].toLowerCase()
	}

	const hash = parseHashCandidate(value)
	if (!hash || String(value).trim().toLowerCase() !== hash) {
		throw new TransferError('X-Firmware-MD5 is not an exact hexadecimal MD5', EXIT.PROTOCOL)
	}
	return hash
}

function collectDownloadHashes(headers, targetMap, source) {
	for (const name of ['x-firmware-md5', 'content-md5', 'etag']) {
		const hash = parseHeaderMd5(headers, name)
		if (!hash) continue
		if (targetMap.size && [...targetMap.values()].some(existing => existing !== hash)) {
			throw new TransferError(`${source} ${name} conflicts with another expected firmware hash`, EXIT.INTEGRITY)
		}
		targetMap.set(`${source}:${name}`, hash)
	}
}

async function pathSize(path) {
	try {
		const stat = await fs.stat(path)
		if (!stat.isFile()) throw new Error('not a regular file')
		return stat.size
	} catch (error) {
		if (error.code === 'ENOENT') return 0
		throw new TransferError(
			`Unable to inspect "${path}": ${sanitizeDeviceText(error.message)}`,
			EXIT.LOCAL_IO,
			{ cause: error }
		)
	}
}

async function writePart(path, data, append) {
	try {
		await fs.mkdir(dirname(path), { recursive: true })
		await fs.writeFile(path, data, { flag: append ? 'a' : 'w' })
	} catch (error) {
		throw new TransferError(
			`Unable to write "${path}": ${sanitizeDeviceText(error.message)}`,
			EXIT.LOCAL_IO,
			{ cause: error }
		)
	}
}

async function removePart(path) {
	try {
		await fs.unlink(path)
	} catch (error) {
		if (error.code !== 'ENOENT') {
			throw new TransferError(
				`Unable to remove invalid partial download "${path}": ${sanitizeDeviceText(error.message)}`,
				EXIT.LOCAL_IO,
				{ cause: error }
			)
		}
	}
}

async function verifiedDownload(resolver, config, destination, logger) {
	const target = resolve(destination)
	const part = `${target}.part`
	const downloadUrl = endpoint(config.updateUrl, '/firmware/download')
	const info = await fetchFirmwareInfo(resolver, config)
	const expectedHashes = new Map([['/update/info', info.hash]])

	const head = await requestBuffer(resolver, downloadUrl, {
		method: 'HEAD',
		headers: commonHeaders(config),
		timeoutMs: config.requestTimeoutMs,
		maxResponseBytes: 1024,
		safeRefresh: true
	})
	if (head.statusCode < 200 || head.statusCode >= 300) {
		throw new TransferError(
			`Firmware download metadata returned HTTP ${head.statusCode}${statusBody(head)}`,
			EXIT.PROTOCOL
		)
	}
	const totalLength = parseContentLength(head.headers, 'Firmware download metadata')
	if (totalLength <= 0 || totalLength > config.maxDownloadBytes) {
		throw new TransferError('Firmware download size exceeds the configured safety limit', EXIT.PROTOCOL)
	}
	collectDownloadHashes(head.headers, expectedHashes, 'HEAD')

	let offset = config.resume ? await pathSize(part) : 0
	if (!config.resume || offset > totalLength) {
		await writePart(part, Buffer.alloc(0), false)
		offset = 0
	}

	if (offset < totalLength) {
		const requestHeaders = {
			...commonHeaders(config),
			...(offset > 0 ? { Range: `bytes=${offset}-` } : {})
		}
		const response = await requestBuffer(resolver, downloadUrl, {
			headers: requestHeaders,
			timeoutMs: config.requestTimeoutMs,
			maxResponseBytes: totalLength,
			safeRefresh: true
		})

		let append = false
		let expectedBodyLength = totalLength
		if (offset > 0 && response.statusCode === 206) {
			const contentRange = String(response.headers['content-range'] || '')
			const match = /^bytes (\d+)-(\d+)\/(\d+)$/.exec(contentRange)
			if (!match || Number(match[1]) !== offset || Number(match[2]) !== totalLength - 1 || Number(match[3]) !== totalLength) {
				throw new TransferError('Range response Content-Range does not match the requested resume offset', EXIT.PROTOCOL)
			}
			append = true
			expectedBodyLength = totalLength - offset
		} else if (response.statusCode === 200) {
			offset = 0
		} else {
			throw new TransferError(
				`Firmware download returned HTTP ${response.statusCode}${statusBody(response)}`,
				EXIT.PROTOCOL
			)
		}

		const responseLength = parseContentLength(response.headers, 'Firmware download')
		if (responseLength !== expectedBodyLength || response.body.length !== expectedBodyLength) {
			throw new TransferError(
				`Firmware download length mismatch (expected ${expectedBodyLength}, received ${response.body.length})`,
				EXIT.INTEGRITY
			)
		}
		collectDownloadHashes(response.headers, expectedHashes, 'GET')
		await writePart(part, response.body, append)
	}

	const finalSize = await pathSize(part)
	if (finalSize !== totalLength) {
		throw new TransferError(
			`Partial firmware size mismatch (expected ${totalLength}, received ${finalSize})`,
			EXIT.INTEGRITY
		)
	}

	let downloaded
	try {
		downloaded = await fs.readFile(part)
	} catch (error) {
		throw new TransferError(
			`Unable to verify "${part}": ${sanitizeDeviceText(error.message)}`,
			EXIT.LOCAL_IO,
			{ cause: error }
		)
	}
	const actualMd5 = md5Hex(downloaded)
	const mismatched = [...expectedHashes.entries()].filter(([, expected]) => expected !== actualMd5)
	if (mismatched.length) {
		await removePart(part)
		throw new TransferError(
			`Downloaded firmware MD5 ${actualMd5} does not match ${mismatched.map(([source, hash]) => `${source} ${hash}`).join(', ')}`,
			EXIT.INTEGRITY
		)
	}

	try {
		await fs.rename(part, target)
	} catch (error) {
		throw new TransferError(
			`Unable to atomically publish "${target}": ${sanitizeDeviceText(error.message)}`,
			EXIT.LOCAL_IO,
			{ cause: error }
		)
	}
	logger.success(`Verified firmware download: ${target} (${totalLength} bytes, MD5 ${actualMd5})`)
	return { path: target, size: totalLength, md5: actualMd5 }
}

async function initializeResolver(config, logger) {
	const resolver = new EndpointResolver(config.updateUrl, logger)
	await resolver.initialize()
	return resolver
}

export async function performUpload(config, logger = createLogger(config)) {
	const firmware = await prepareFirmware(config)
	logger.info(
		`Validated ${firmware.fileName}: compressed ${firmware.compressed.length} bytes ` +
		`(${firmware.compressedMd5}), raw ${firmware.rawSize} bytes (${firmware.rawMd5})`
	)

	if (config.mode === 'check') {
		logger.success('Firmware and strict two-record manifest are valid; no device was contacted.')
		return { checked: true, firmware }
	}

	const resolver = await initializeResolver(config, logger)
	const before = await fetchFirmwareInfo(resolver, config)
	logger.detail(`Device firmware before transfer: ${before.hash}`)
	if (before.hash === firmware.rawMd5 && !config.force) {
		logger.success('Device already has the requested firmware; upload skipped. Use --force to override.')
		return { skipped: true, firmware }
	}

	if (config.backupPath) {
		logger.info('Downloading a verified pre-update backup…')
		await verifiedDownload(resolver, config, config.backupPath, logger)
	}

	logger.info(`Uploading ${firmware.fileName}…`)
	await uploadCompressedFirmware(resolver, config, firmware)
	logger.success('Device returned the exact upload acknowledgement.')

	if (config.verify) {
		await verifyAfterReboot(resolver, config, firmware.rawMd5, logger)
		logger.success(`Post-reboot firmware hash verified: ${firmware.rawMd5}`)
	} else {
		logger.warn('Post-reboot verification was explicitly disabled with --no-verify.')
	}

	return { uploaded: true, firmware }
}

export async function performDownload(config, logger = createLogger(config)) {
	const resolver = await initializeResolver(config, logger)
	return await verifiedDownload(resolver, config, config.downloadPath, logger)
}

function redactSecrets(message, config) {
	let result = sanitizeDeviceText(message, 8192)
	if (config?.authorization) {
		const values = [config.authorization]
		const separator = config.authorization.indexOf(' ')
		if (separator >= 0 && separator + 1 < config.authorization.length) {
			values.push(config.authorization.slice(separator + 1))
		}
		for (const value of values.sort((left, right) => right.length - left.length)) {
			if (value) result = result.replaceAll(value, '[redacted]')
		}
	}
	return result
}

async function runWatch(config, logger) {
	const firmwarePath = resolve(config.firmwarePath)
	const manifestPath = resolve(config.manifestPath)
	let running = false
	let pending = false
	let debounceTimer = null
	let closing = false

	const transfer = async () => {
		if (running) {
			pending = true
			return
		}
		running = true
		do {
			pending = false
			try {
				await performUpload(config, logger)
			} catch (error) {
				logger.error(redactSecrets(error.message || error, config))
			}
		} while (pending && !closing)
		running = false
	}

	const watcher = watch([firmwarePath, manifestPath], {
		ignoreInitial: true,
		awaitWriteFinish: { stabilityThreshold: 500, pollInterval: 100 }
	})
	watcher.on('ready', () => {
		logger.info(`Watching:\n  ${firmwarePath}\n  ${manifestPath}`)
	})
	watcher.on('all', (eventName, path) => {
		logger.detail(`${eventName}: ${path}`)
		clearTimeout(debounceTimer)
		debounceTimer = setTimeout(transfer, 500)
	})
	watcher.on('error', error => logger.error(`Watch error: ${redactSecrets(error.message || error, config)}`))

	await new Promise(resolvePromise => {
		const shutdown = async () => {
			if (closing) return
			closing = true
			clearTimeout(debounceTimer)
			await watcher.close()
			resolvePromise()
		}
		process.once('SIGINT', shutdown)
		process.once('SIGTERM', shutdown)
	})
	logger.info('Watcher stopped.')
	return closing
}

export async function main(argv = process.argv.slice(2), env = process.env) {
	let config
	try {
		config = parseArguments(argv, env)
		if (config.help) {
			console.log(usage())
			return EXIT.OK
		}

		const logger = createLogger(config)
		if (config.mode === 'watch') {
			const interrupted = await runWatch(config, logger)
			return interrupted ? EXIT.INTERRUPTED : EXIT.OK
		}
		if (config.mode === 'download') {
			await performDownload(config, logger)
			return EXIT.OK
		}
		await performUpload(config, logger)
		return EXIT.OK
	} catch (error) {
		const exitCode = error instanceof TransferError ? error.exitCode : EXIT.PROTOCOL
		const logger = createLogger(config || {})
		logger.error(redactSecrets(error?.message || error, config))
		return exitCode
	}
}

const isEntryPoint = process.argv[1] && resolve(process.argv[1]) === resolve(fileURLToPath(import.meta.url))
if (isEntryPoint) {
	const exitCode = await main()
	process.exitCode = exitCode
}
