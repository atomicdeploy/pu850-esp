import http from 'http'
import { spawn } from 'child_process'
import { writeFileSync, unlinkSync, existsSync, mkdirSync, readFileSync } from 'fs'
import { createHash } from 'crypto'
import { dirname, join } from 'path'
import { fileURLToPath } from 'url'

const __dirname = dirname(fileURLToPath(import.meta.url))

// Test configuration
const TEST_PORT = 18765
const TEST_FIRMWARE_CONTENT = Buffer.from('This is test firmware content for upload verification')
const TEST_FIRMWARE_PATH = join(__dirname, 'test-firmware.bin')
const EXPECTED_MD5 = createHash('md5').update(TEST_FIRMWARE_CONTENT).digest('hex')

// Colors for output
const GREEN = '\x1b[32m'
const RED = '\x1b[31m'
const YELLOW = '\x1b[33m'
const CYAN = '\x1b[36m'
const RESET = '\x1b[0m'

function log(color, prefix, message) {
    console.log(`${color}[${prefix}]${RESET} ${message}`)
}

// Track received uploads
let receivedUploads = []

// Create mock API server
function createMockServer() {
    return new Promise((resolve, reject) => {
        const server = http.createServer((req, res) => {
            if (req.method === 'POST' && req.url === '/update') {
                let body = []
                
                req.on('data', chunk => body.push(chunk))
                req.on('end', () => {
                    const data = Buffer.concat(body)
                    const contentType = req.headers['content-type'] || ''
                    
                    // Parse multipart form data
                    const boundary = contentType.split('boundary=')[1]
                    if (!boundary) {
                        res.writeHead(400, { 'Content-Type': 'text/plain' })
                        res.end('Missing boundary in Content-Type')
                        return
                    }
                    
                    // Simple multipart parser
                    const parts = data.toString('binary').split('--' + boundary)
                    const upload = { md5: null, firmware: null, firmwareSize: 0 }
                    
                    for (const part of parts) {
                        if (part.includes('name="MD5"')) {
                            // Extract MD5 value
                            const match = part.match(/\r\n\r\n([\s\S]*?)\r\n/)
                            if (match) {
                                upload.md5 = match[1].trim()
                            }
                        } else if (part.includes('name="firmware"')) {
                            // Extract firmware content
                            const headerEnd = part.indexOf('\r\n\r\n')
                            if (headerEnd !== -1) {
                                const content = part.substring(headerEnd + 4)
                                // Remove trailing \r\n
                                upload.firmware = Buffer.from(content.replace(/\r\n$/, ''), 'binary')
                                upload.firmwareSize = upload.firmware.length
                            }
                        }
                    }
                    
                    // Verify the upload
                    if (upload.firmware) {
                        const calculatedMd5 = createHash('md5').update(upload.firmware).digest('hex')
                        upload.calculatedMd5 = calculatedMd5
                        upload.md5Match = upload.md5 === calculatedMd5
                    }
                    
                    receivedUploads.push(upload)
                    
                    log(CYAN, 'SERVER', `Received upload: MD5=${upload.md5}, Size=${upload.firmwareSize}, Match=${upload.md5Match}`)
                    
                    res.writeHead(200, { 'Content-Type': 'text/plain' })
                    res.end('ok!')
                })
                
                req.on('error', err => {
                    log(RED, 'SERVER', `Request error: ${err.message}`)
                    res.writeHead(500, { 'Content-Type': 'text/plain' })
                    res.end('Error')
                })
            } else if (req.url === '/info' || req.url === '/updateinfo') {
                res.writeHead(200, { 'Content-Type': 'text/plain' })
                res.end(`hostname: test-device
firmware hash: ${EXPECTED_MD5}
program usage: 50%
build: Test Build
uptime: 0 days 0 hours 0 minutes`)
            } else {
                res.writeHead(404, { 'Content-Type': 'text/plain' })
                res.end('Not Found')
            }
        })
        
        server.on('error', reject)
        server.listen(TEST_PORT, () => {
            log(GREEN, 'SERVER', `Mock API server listening on port ${TEST_PORT}`)
            resolve(server)
        })
    })
}

// Create test firmware file
function createTestFirmware() {
    writeFileSync(TEST_FIRMWARE_PATH, TEST_FIRMWARE_CONTENT)
    log(GREEN, 'TEST', `Created test firmware: ${TEST_FIRMWARE_PATH}`)
    log(GREEN, 'TEST', `Expected MD5: ${EXPECTED_MD5}`)
}

// Clean up test files
function cleanup() {
    if (existsSync(TEST_FIRMWARE_PATH)) {
        unlinkSync(TEST_FIRMWARE_PATH)
    }
    if (existsSync(TEST_FIRMWARE_PATH + '.md5')) {
        unlinkSync(TEST_FIRMWARE_PATH + '.md5')
    }
}

// Run the upload tool
function runUploadTool() {
    return new Promise((resolve, reject) => {
        log(YELLOW, 'TEST', 'Starting upload tool in immediate mode...')
        
        const uploadProcess = spawn('node', ['upload.js', '--immediate', TEST_FIRMWARE_PATH], {
            cwd: __dirname,
            env: {
                ...process.env,
                UPDATE_API: `http://127.0.0.1:${TEST_PORT}/update`,
                // Disable notifications
                NO_NOTIFIER: '1'
            },
            stdio: ['pipe', 'pipe', 'pipe']
        })
        
        let stdout = ''
        let stderr = ''
        
        uploadProcess.stdout.on('data', data => {
            stdout += data.toString()
            process.stdout.write(data)
        })
        
        uploadProcess.stderr.on('data', data => {
            stderr += data.toString()
            process.stderr.write(data)
        })
        
        // Timeout after 30 seconds
        const timeoutId = setTimeout(() => {
            uploadProcess.kill()
            reject(new Error('Upload tool timed out'))
        }, 30000)
        
        uploadProcess.on('close', code => {
            clearTimeout(timeoutId)
            resolve({ code, stdout, stderr })
        })
        
        uploadProcess.on('error', err => {
            clearTimeout(timeoutId)
            reject(err)
        })
    })
}

// Main test function
async function runTests() {
    let server = null
    let exitCode = 0
    
    console.log('\n' + '='.repeat(60))
    log(CYAN, 'TEST', 'Upload Tool Test Suite')
    console.log('='.repeat(60) + '\n')
    
    try {
        // Setup
        cleanup()
        createTestFirmware()
        server = await createMockServer()
        
        // Run upload tool
        const result = await runUploadTool()
        
        console.log('\n' + '-'.repeat(60))
        log(CYAN, 'RESULTS', 'Test Results')
        console.log('-'.repeat(60) + '\n')
        
        // Verify results
        const tests = [
            {
                name: 'Upload tool exits with code 0',
                pass: result.code === 0,
                actual: result.code,
                expected: 0
            },
            {
                name: 'Server received exactly one upload',
                pass: receivedUploads.length === 1,
                actual: receivedUploads.length,
                expected: 1
            },
            {
                name: 'Received MD5 matches expected',
                pass: receivedUploads[0]?.md5 === EXPECTED_MD5,
                actual: receivedUploads[0]?.md5,
                expected: EXPECTED_MD5
            },
            {
                name: 'Firmware content MD5 matches provided MD5',
                pass: receivedUploads[0]?.md5Match === true,
                actual: receivedUploads[0]?.calculatedMd5,
                expected: receivedUploads[0]?.md5
            },
            {
                name: 'Firmware size is correct',
                pass: receivedUploads[0]?.firmwareSize === TEST_FIRMWARE_CONTENT.length,
                actual: receivedUploads[0]?.firmwareSize,
                expected: TEST_FIRMWARE_CONTENT.length
            }
        ]
        
        let passed = 0
        let failed = 0
        
        for (const test of tests) {
            if (test.pass) {
                log(GREEN, 'PASS', test.name)
                passed++
            } else {
                log(RED, 'FAIL', `${test.name}`)
                log(RED, '    ', `Expected: ${test.expected}`)
                log(RED, '    ', `Actual: ${test.actual}`)
                failed++
            }
        }
        
        console.log('\n' + '='.repeat(60))
        if (failed === 0) {
            log(GREEN, 'SUMMARY', `All ${passed} tests passed!`)
        } else {
            log(RED, 'SUMMARY', `${failed} of ${passed + failed} tests failed`)
            exitCode = 1
        }
        console.log('='.repeat(60) + '\n')
        
    } catch (error) {
        log(RED, 'ERROR', error.message)
        console.error(error)
        exitCode = 1
    } finally {
        // Cleanup
        if (server) {
            server.close()
            log(YELLOW, 'CLEANUP', 'Stopped mock server')
        }
        cleanup()
        log(YELLOW, 'CLEANUP', 'Removed test files')
    }
    
    process.exit(exitCode)
}

// Run tests
runTests()
