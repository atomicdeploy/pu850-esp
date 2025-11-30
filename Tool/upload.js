import got from 'got'
import dns from 'dns/promises'
import { watch } from 'chokidar'
import { existsSync, statSync, readFileSync, realpathSync } from 'fs'
import { basename } from 'path'
import FormData from 'form-data'
import { createHash } from 'crypto'
import chalk from 'chalk'
import notifier from 'node-notifier'

if (process.argv.includes('--help')) {
	console.log(`${chalk.green.bold`Usage:`} ${basename(process.argv[0]).replace(/\.exe$/gi, '')} ${basename(process.argv[1])} ${chalk.yellow('filename.bin')}\n`)
	console.log(`${chalk.yellow`You must also set the environment variable \`UPDATE_API\` to the URL of the update API.`}`)
	console.log(`${chalk.magenta`Example:`} set UPDATE_API=http://PU850.local:80/update`)
	process.exit(0)
}

const headers = {
	'User-Agent': 'ASA-Update-Tool/1.0.0'
}

let UPDATE_API = process.env.UPDATE_API

if (!UPDATE_API) {
	console.error('Please provide the `UPDATE_API` environment variable')
	process.exit(1)
}

try {
	// Validate the URL
	const url = new URL(UPDATE_API)

	// Check if the host of the URL is does not have a gTLD, or if it is an mDNS (.local) address
	if (url.hostname.match(/^[a-z0-9-]+(\.local)?$/i)) {
		console.info(chalk.yellow`Resolving hostname:`, url.hostname)
		const result = await dns.lookup(url.hostname)
		url.hostname = result.address
		UPDATE_API = url.toString()
		console.info(chalk.green`Hostname successfully resolved:`, result.address)
		// notifier.notify({ title: 'ℹ️ Using resolved address', message: UPDATE_API, sound: false, icon: 'None' })
	}
} catch (error) {
	console.error('The provided `UPDATE_API` is not a valid URL')
	console.error(error?.message ? chalk.red.bold(error.message) : error)
	process.exit(1)
}

const filePath = process.argv[2]

if (!filePath) {
	console.error('Please provide a file path as an argument')
	process.exit(1)
}

if (!existsSync(filePath)) {
	console.error('The specified file does not exist')
	process.exit(1)
}

const resolvedPath = realpathSync(filePath, { encoding: "utf8" })

const watcher = watch(filePath)

process.stdout.write('\x1b[H\x1b[2J') // clear screen

// onModified(filePath)

// displayInformation(0)

let debounceId = 0

// Event handler for ready
watcher.on('ready', () => {
	console.info(chalk.cyan`Watching for file changes on:\n` + chalk.bold(resolvedPath));
	process.title = `Watching ${basename(resolvedPath)}`
	// notifier.notify({ title: '️ℹ Watching for file changes on:', message: `${resolvedPath}`, sound: false, icon: 'None' })
})

// Event handler for file changes
watcher.on('change', (path) => {
	if (!process.ongoingRequest) {
		process.stdout.write(`\r\x1b[K${chalk.gray('File change detected.')}\r`)
		// process.title = `File modified ${basename(path)}`
	}

	clearTimeout(debounceId)
	debounceId = setTimeout(onModified.bind(onModified, path), 1000)
})

// Event handler for file deletions
watcher.on('unlink', (path) => {
	process.stdout.write('\x1b[H\x1b[2J') // clear screen

	console.info(chalk.yellow('The build file has been removed.'))
	// console.info(chalk.gray('Waiting for the file to be rebuilt'))
	console.info(chalk.gray(path))
})

// Event handler for errors
watcher.on('error', (error) => {
	console.error(chalk.bold.red('An error occurred while watching the file:'))
	console.error(error)
	process.title = `Error watching file`
	notifier.notify({ title: '❌ Error watching for file changes on:', message: `${resolvedPath}`, sound: true, icon: 'None' })
})

// On shutdown, close the watcher
process.on('SIGINT', () => {
	if (process.ongoingRequest && !process.forceExit) {
		console.info(chalk.yellow('\nAn ongoing request is in progress, press Ctrl+C again to force exit.'))
		process.forceExit = true
		return
	}

	process.stdout.write(`\x1b]9;4;0;0\x07`) // clear progress
	process.stdout.write('\x1b[H\x1b[2J') // clear screen
	process.title = ``

	// console.info(chalk.yellow('Stopping the watcher...'));
	watcher.close().then(() => {
		console.info(chalk.green('Watcher stopped.'));
		process.exit(0);
	}).catch((error) => {
		console.error(chalk.red('Error stopping the watcher:'), error);
		process.exit(1);
	});
});

async function onModified(filePath) {
	const fileName = basename(filePath)

	if (existsSync(`${filePath}/../~local.h`) || existsSync(`${filePath}.gz`)) {
		process.stdout.write(`\r\x1b[K${chalk.gray('File is still being built...')}\r`);
		return setTimeout(onModified.bind(onModified, filePath), 1000)
	}

	if (!existsSync(`${filePath}.md5`)) {
		process.stdout.write(`\r\x1b[K${chalk.gray('Waiting for the MD5 file...')}\r`);
		return setTimeout(onModified.bind(onModified, filePath), 1000)
	}

	// Read and parse the contents of the `.md5` which is the output of the `md5sum` command
	const md5File = readFileSync(`${filePath}.md5`, { encoding: 'utf8' }).trim(),
		md5Lines = md5File.split(/[\r\n]+/g).map(line => line.trim()),
		md5List = {}

	if (!md5File || md5Lines.length < 1)
		return console.error(chalk.red.bold('MD5 file is empty.'))

	let skip = false

	md5Lines.forEach(line => {
		line = line.replace(/\s+/g, ' ')

		const index = line.indexOf(' ')

		let [md5, name] = [line.substring(0, index).trim(), line.substring(index + 1).trim()]

		name = basename(name.replace(/^\*/, '')) // + tag
		md5 = md5.replace(/[\\]/gi, '')

		if (md5List[name] && md5List[name] !== md5) {
			console.error(md5List[name], md5)
			throw new Error(`Multiple MD5 values found for "${name}"`)
		}

		md5List[name] = md5

		if (process.ongoingRequest) {
			skip = true

			// Check if the there is a new file available
			if (md5 !== process.lastUploadedMD5 && name.includes(' (compressed)')) {
				process.stdout.write(`\r\x1b[K${chalk.gray('An ongoing request is in progress...')}\r`);
				clearTimeout(process.ongoingTimerId)
				process.ongoingTimerId = setTimeout(onModified.bind(onModified, filePath), 1000)
			}
		}
	})

	if (skip) return

	process.stdout.write('\x1b[H\x1b[2J') // clear screen

	console.info(`File "${chalk.yellow.bold(fileName)}" has been modified.`)

	try {
		const stats = statSync(filePath)

		console.info('Size:', stats.size, 'bytes')
		console.info('Time:', stats.mtime) // Last modified time, .toLocaleString()

		const fileData = readFileSync(filePath)

		const hash = createHash('md5')
		hash.update(fileData)

		const md5 = hash.digest('hex')

		console.info('MD5 Hash:', chalk.cyan(md5))

		let mismatch = false

		if (md5 !== md5List[fileName + ' (compressed)']) {
			console.error(chalk.red.bold('MD5 mismatch!'))
			console.error('Expected:', md5List[fileName + ' (compressed)'] || 'Unknown')
			console.error('Calculated:', md5)
			mismatch = true
		}

		if (mismatch) return

		const formData = new FormData()
		formData.append('MD5', md5)
		formData.append('firmware', fileData)

		process.stdout.write('Uploading file...')
		process.title = `Uploading firmware`

		process.stdout.write(`\x1b]9;4;3;0\x07`) // Set progress to indeterminate state

		process.ongoingRequest = true
		process.lastUploadedMD5 = md5

		process.forceExit = false

		const response = await got.post(UPDATE_API, {
			body: formData,
			headers: {
				...formData.getHeaders(),
				...headers,
			}
		}).on('uploadProgress', (progressEvent) => {
			if (progressEvent.transferred == 0)
			{
				process.stdout.write(`\r\x1b[K${chalk.gray('Upload started')}\r`)
				return
			}

			const percentCompleted = Math.round((progressEvent.transferred / progressEvent.total) * 100)
			process.stdout.write(`\r\x1b[K${chalk.gray(`Upload progress: ${percentCompleted}%`)}\r`)

			if (progressEvent.transferred === progressEvent.total) {
				process.stdout.write(`\r\x1b[K${chalk.gray('Upload completed!')}\r`)
				process.title = `Upload completed, waiting for response`
			}
		})

		process.stdout.write(`\r\x1b[K`) // clear line

		console.log(chalk.green.bold('Upload successful!'))

		process.title = `Upload successful`

		if (response.body !== 'ok!') {
			console.error(chalk.red.bold('The API response was not as expected!'))
			console.error('Response:', response.body)
			process.title = `Update failed`
			return
		}

		process.expectedFirmwareHash = md5List[fileName]

		await displayInformation(2000)

	} catch (error) {
		process.stdout.write(`\x1b]9;4;0;0\x07`) // clear progress

		process.stdout.write(`\r\x1b[K`) // clear line

		process.title = `Upload failed`
		notifier.notify({ title: '❌ Error while uploading firmware', message: error?.message || `See console for details`, sound: true, icon: 'None' })

		if (error.response && error.response.headers['content-type']?.includes('text/plain')) {
			console.error('❌ Received an error with status code:', error.response.statusCode)
			console.error(chalk.red.bold(error.response.body || error.response.statusMessage))
		} else if (error.message) {
			console.error(chalk.red.bold`❌ Error`, error.message)
		} else {
			console.error(chalk.red.bold`❌ Error`)
			console.error(error)
		}
	}

	process.ongoingRequest = false
}

async function displayInformation(waitInitial = 2000) {
	process.stdout.write(`\r\x1b[K${chalk.gray(`Getting information...`)}`)

	await new Promise(resolve => setTimeout(resolve, waitInitial))

	const waitFor = 20 * 1000

	let start = Date.now()

	let timerId = setInterval(async () => {
		const elapsed = Math.min((Date.now() - start) / (waitFor), 1), remaining = Math.max(Math.round((waitFor - Date.now() + start) / 1000), 0)

		const steps = 40, progress = Math.round(elapsed * steps)

		let bar = '━'.repeat(progress), text = `${Math.round(elapsed * 100)}%`

		bar = bar.substring(0, bar.length - text.length)

		const remainingBar = '─'.repeat(steps - bar.length - text.length)

		const pattern = [ '⡿', '⣟', '⣯', '⣷', '⣾', '⣽', '⣻', '⢿' ]

		// process.stdout.write(`\r\x1b[K${chalk.yellow(bar) + chalk.yellow.bold(text) + chalk.gray(remainingBar)}\r`) // (${remaining}s)

		process.stdout.write(`\r\x1b[K${elapsed < 1 ? chalk.yellow(pattern[Math.floor(elapsed * 200) % pattern.length]) : "⏳"} ${((Date.now() - start) / 1000).toFixed(0)}s`)

		process.title = (elapsed < 1 ? pattern[Math.floor(elapsed * 200) % pattern.length] : "⏳") + ` Waiting (${((Date.now() - start) / 1000).toFixed(0)}s)`

		// Set the progress using ESC ] 9 ; 4 ; <state> ; <progress> BEL
		process.stdout.write(`\x1b]9;4;1;${Math.round(elapsed * 100)}\x07`)

		if (elapsed >= 1) {
			// clearInterval(timerId)
			// process.stdout.write(`\r\x1b[K`)
		}
	}, 100)

	try {
		const request = await got(new URL('info', UPDATE_API), {
			headers,
			timeout: {
				connect: 2000,
				request: 2000,
				socket: 2000
			},
			retry: {
				limit: 20,
				errorCodes: ['ETIMEDOUT', 'ECONNRESET', 'EADDRINUSE', 'ECONNREFUSED', 'EPIPE', 'ENETUNREACH', 'EAI_AGAIN', 'ENOTFOUND']
			}
		})

		process.stdout.write(`\x1b]9;4;0;0\x07`) // clear progress

		process.stdout.write(`\r\x1b[K`) // clear line

		const info = {}

		request.body.split(/[\r\n]+/).forEach(line => {
			const index = line.indexOf(':')

			if (index < 0) return

			const key = line.substring(0, index).trim()
			const value = line.substring(index + 1).trim()

			if (!key || !value) return

			info[key] = value

			if (!['hostname', 'firmware hash', 'program usage', 'build', 'uptime'].includes(key)) return

			console.log(chalk.cyan.green(key), value)
		})

		if (!process.expectedFirmwareHash) {
			console.log(chalk.cyan.gray('Missing Expected Firmware Hash'))
			return
		}

		if (info['firmware hash'] !== process.expectedFirmwareHash) {
			process.stdout.write('\u0007'); // Beep
			console.error(chalk.red.bold.inverse('❌ Firmware hash mismatch!'))
			console.error('Expected:', process.expectedFirmwareHash || 'Unknown')
			console.error('Received:', info['firmware hash'])
			process.title = `❌ Firmware hash mismatch ${basename(filePath)}`
			notifier.notify({ title: '❌ Firmware hash mismatch', message: ('Expected: ' + process.expectedFirmwareHash || 'Unknown') + "\n" + ('Received: ' + info['firmware hash']), sound: true, icon: 'None' })
			return
		}

		let message = info['firmware hash']

		if (info['build']) {
			const match = info['build'].match(/(\w{3}\s+\d+\s+\d{4}\s+\d{2}:\d{2}:\d{2})/)
			if (match) {
				const buildDate = new Date(match[1])
				if (!isNaN(buildDate))
					message += `\nBuilt: ${humanTimeDiff(buildDate)}`
			}
		}

		if (info['uptime'])
			message += `\nUptime: ${info['uptime']}`;

		console.log(chalk.green.bold.inverse('✅ Firmware hash matches'))
		process.title = `✅ Firmware ${basename(filePath)} uploaded`
		notifier.notify({ title: '✅ Firmware successfully updated', message: message.trim(), sound: false, icon: 'None' })
	} catch (error) {
		process.stdout.write(`\r\x1b[K`) // clear line
		console.error(chalk.red.bold`Error fetching information:`, error.message)
	} finally {
		clearInterval(timerId)
		setTimeout(() => process.stdout.write(`\x1b]9;4;0;0\x07`), 200) // clear progress
	}
}

function humanTimeDiff(fromDate, toDate = new Date()) {
	const ms = toDate - fromDate
	const sec = Math.floor(ms / 1000)
	const min = Math.floor(sec / 60)
	const hr = Math.floor(min / 60)
	const day = Math.floor(hr / 24)

	if (day > 0) return `${day} day${day !== 1 ? 's' : ''} ago`
	if (hr > 0) return `${hr} hour${hr !== 1 ? 's' : ''} ago`
	if (min > 0) return `${min} minute${min !== 1 ? 's' : ''} ago`
	return `${sec} second${sec !== 1 ? 's' : ''} ago`
}

function humanBytes(bytes) {
	const units = ['B', 'KB', 'MB', 'GB', 'TB'];
	let i = 0;
	while (bytes >= 1024 && i < units.length - 1) {
		bytes /= 1024;
		i++;
	}
	return `${bytes.toFixed(i === 0 ? 0 : 2)} ${units[i]}`;
}
