// The gateway serves plain HTTP, where browsers withhold the Clipboard API.
export function copyText(value: string): boolean {
  const field = document.createElement('textarea')
  field.value = value
  field.readOnly = true
  field.style.position = 'fixed'
  field.style.opacity = '0'
  document.body.append(field)
  field.select()
  // iOS Safari ignores select() on a read-only field.
  field.setSelectionRange(0, value.length)
  try {
    return document.execCommand('copy')
  } catch {
    return false
  } finally {
    field.remove()
  }
}
