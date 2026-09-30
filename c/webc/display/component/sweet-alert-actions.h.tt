<script src="https://cdn.jsdelivr.net/npm/sweetalert2@11"></script>
<script>
    document.addEventListener('livewire:initialized', () => {

        Livewire.on('swal:multi-action', (data) => {
            const payload = data[0];

            Swal.fire({
                title: payload.title,
                text: payload.text,
                icon: 'warning',
                showCancelButton: true,
                showDenyButton: true, // Always show 3 buttons

                confirmButtonText: payload.confirmText,
                denyButtonText: payload.denyText,
                cancelButtonText: 'Cancel',

                confirmButtonColor: payload.confirmColor || '#f59e0b', // Default Orange (Trash)
                denyButtonColor: '#ef4444', // Red (Force Delete)

            }).then((result) => {
                if (result.isConfirmed) {
                    // Button 1 Clicked (Trash OR Restore)
                    Livewire.dispatch('executeAction', {
                        id: payload.id,
                        action: payload.confirmAction
                    });
                } else if (result.isDenied) {
                    // Button 2 Clicked (Force Delete)
                    Livewire.dispatch('executeAction', {
                        id: payload.id,
                        action: payload.denyAction
                    });
                }
            });
        });

        // Standard Success Listener
        Livewire.on('swal:success', (data) => {
            Swal.fire({
                title: 'Success!',
                text: data[0].message,
                icon: 'success',
                timer: 2000,
                showConfirmButton: false
            });
        });
    });
</script>
